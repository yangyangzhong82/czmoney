#include "czmoney/money.h"
#include "czmoney/money_api.h" // 包含 API 头文件以获取 TransactionLogEntry 定义
#include "czmoney/database_interface.h" // 包含数据库接口
#include "ll/api/mod/NativeMod.h"
#include "ll/api/memory/Hook.h"
// #include <mysql.h> // 不再直接需要 MySQL 头文件
#include <stdexcept>
#include <vector>
#include <string>
#include <variant> // 用于处理 DbValue
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <utility> // For std::move
#include <typeinfo> // For typeid in error logging

namespace czmoney {

// 移除 MySQL 特定的 StatementGuard 和 BindGuard 类

// --- 私有辅助函数实现 ---

// 新增：安全地将 double 金额转换为 int64_t (分)
std::optional<int64_t> MoneyManager::convertDoubleToInt64(double amount, const std::string& context) const {
    // 1. 检查 NaN 和 Infinity
    if (std::isnan(amount) || std::isinf(amount)) {
        mLogger.error("配置中的无效 {} 值 (NaN 或 Infinity): {}", context, amount);
        return std::nullopt;
    }

    // 2. 转换为分 (使用截断)
    double centsDouble = amount * 100.0;

    // 3. 检查转换后的值是否在 int64_t 范围内 (截断前的检查)
    const double min_representable = static_cast<double>(std::numeric_limits<int64_t>::min());
    const double max_representable_plus_one = static_cast<double>(std::numeric_limits<int64_t>::max()) + 1.0; // 加 1.0 考虑截断

    if (centsDouble < min_representable || centsDouble >= max_representable_plus_one) {
        mLogger.error("配置中的 {} 值 {} 转换后超出 int64_t 可表示范围", context, amount);
        return std::nullopt;
    }

    // 4. 安全地转换为 int64_t (执行截断)
    return static_cast<int64_t>(centsDouble);
}


// 检查货币类型是否已配置
bool MoneyManager::isCurrencyConfigured(const std::string& currencyType) const {
    return mConfig.economy.count(currencyType) > 0;
}

// 更新：获取指定货币类型的最低余额 (从 double 转换)
int64_t MoneyManager::getMinimumBalance(const std::string& currencyType) const {
    auto it = mConfig.economy.find(currencyType);
    if (it != mConfig.economy.end()) {
        // 从配置获取 double 值
        double minBalanceDouble = it->second.minimumBalance;
        // 转换并验证
        std::optional<int64_t> minBalanceInt = convertDoubleToInt64(minBalanceDouble, "minimumBalance for " + currencyType);
        if (minBalanceInt.has_value()) {
            return minBalanceInt.value();
        } else {
            // 转换失败，记录错误并返回默认值 0
            mLogger.error("无法转换配置中货币类型 '{}' 的 minimumBalance ({})，将使用默认值 0。", currencyType, minBalanceDouble);
            return 0LL;
        }
    }
    // 如果未找到特定配置，记录警告并返回默认值 0
    mLogger.warn("未在配置中找到货币类型 '{}' 的最低余额设置，将使用默认值 0。", currencyType);
    return 0LL;
}


// MoneyManager 构造函数实现
MoneyManager::MoneyManager(db::IDatabaseConnection& dbConn, const Config& config) : // 使用接口引用
    mDbConnection(dbConn), // 初始化数据库连接接口引用成员
    mConfig(config),       // 初始化配置引用成员
    mLogger(ll::mod::NativeMod::current()->getLogger()) // 初始化日志记录器引用成员
{
    // 构造时检查数据库连接状态
    if (!mDbConnection.isConnected()) {
        mLogger.error("MoneyManager 初始化时数据库连接未建立！");
        // 根据实际需求，这里可以考虑抛出异常来阻止无效的 MoneyManager 实例创建
        // throw std::runtime_error("数据库未连接，无法初始化 MoneyManager");
    }
}

// 初始化数据库表的实现
bool MoneyManager::initializeTable() {
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法初始化货币表：数据库未连接。");
        return false;
    }

    std::string dbType = mDbConnection.getDbType();
    std::string createBalancesTableSQL;

    if (dbType == "mysql") {
        createBalancesTableSQL = R"(
            CREATE TABLE IF NOT EXISTS player_balances (
                id INT AUTO_INCREMENT PRIMARY KEY,
                uuid VARCHAR(36) NOT NULL,
                currency_type VARCHAR(50) NOT NULL,
                amount BIGINT NOT NULL DEFAULT 0,
                last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                UNIQUE KEY unique_player_currency (uuid, currency_type)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
        )";
    } else if (dbType == "sqlite") {
        createBalancesTableSQL = R"(
            CREATE TABLE IF NOT EXISTS player_balances (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                uuid TEXT NOT NULL,
                currency_type TEXT NOT NULL,
                amount INTEGER NOT NULL DEFAULT 0,
                last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE (uuid, currency_type)
            );
        )";
    } else {
        mLogger.error("不支持的数据库类型 '{}'，无法创建 player_balances 表。", dbType);
        return false;
    }

    try {
        mDbConnection.execute(createBalancesTableSQL);
        mLogger.info("'player_balances' 表初始化成功 (类型: {}).", dbType);

        // 初始化流水日志表
        if (!initializeLogTable()) {
            mLogger.error("初始化 'economy_log' 表失败。");
            return false;
        }

        return true; // 全部成功

    } catch (const db::DatabaseException& e) {
        mLogger.error("创建或验证 'player_balances' 表失败: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        mLogger.error("创建或验证 'player_balances' 表时发生意外错误: {}", e.what());
        return false;
    }
} // initializeTable 结束

// 新增：初始化经济流水日志表的实现
bool MoneyManager::initializeLogTable() {
    // 注意：此函数现在由 initializeTable 调用，连接检查可以省略

    std::string dbType = mDbConnection.getDbType();
    std::string createLogTableSQL;
    std::vector<std::string> createIndexSQLs;

    if (dbType == "mysql") {
        createLogTableSQL = R"(
            CREATE TABLE IF NOT EXISTS economy_log (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                uuid VARCHAR(36) NOT NULL,
                currency_type VARCHAR(50) NOT NULL,
                change_amount BIGINT NOT NULL,
                previous_amount BIGINT NOT NULL,
                reason1 VARCHAR(255) DEFAULT NULL,
                reason2 VARCHAR(255) DEFAULT NULL,
                reason3 VARCHAR(255) DEFAULT NULL,
                INDEX idx_uuid (uuid),
                INDEX idx_currency_type (currency_type),
                INDEX idx_timestamp (timestamp)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
        )";
        // MySQL 在 CREATE TABLE 中创建索引
    } else if (dbType == "sqlite") {
        createLogTableSQL = R"(
            CREATE TABLE IF NOT EXISTS economy_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                uuid TEXT NOT NULL,
                currency_type TEXT NOT NULL,
                change_amount INTEGER NOT NULL,
                previous_amount INTEGER NOT NULL,
                reason1 TEXT DEFAULT NULL,
                reason2 TEXT DEFAULT NULL,
                reason3 TEXT DEFAULT NULL
            );
        )";
        // SQLite 需要单独创建索引
        createIndexSQLs.push_back("CREATE INDEX IF NOT EXISTS idx_uuid ON economy_log (uuid);");
        createIndexSQLs.push_back("CREATE INDEX IF NOT EXISTS idx_currency_type ON economy_log (currency_type);");
        createIndexSQLs.push_back("CREATE INDEX IF NOT EXISTS idx_timestamp ON economy_log (timestamp);");
    } else {
         mLogger.error("不支持的数据库类型 '{}'，无法创建 economy_log 表。", dbType);
        return false;
    }


    try {
        mDbConnection.execute(createLogTableSQL);
        for(const auto& sql : createIndexSQLs) {
            mDbConnection.execute(sql);
        }
        mLogger.info("'economy_log' 表和索引初始化成功 (类型: {}).", dbType);
        return true;
    } catch (const db::DatabaseException& e) {
        mLogger.error("创建或验证 'economy_log' 表或索引失败: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        mLogger.error("创建或验证 'economy_log' 表或索引时发生意外错误: {}", e.what());
        return false;
    }
}


// 检查玩家账户是否存在的实现
bool MoneyManager::hasAccount(const std::string& uuid, const std::string& currencyType) {
     // 通过尝试获取余额并检查结果（std::optional）是否包含值来判断账户是否存在
     return getPlayerBalance(uuid, currencyType).has_value();
}


// 获取玩家余额的实现 (不初始化) - 使用 IDatabaseConnection::query
std::optional<int64_t> MoneyManager::getPlayerBalance(const std::string& uuid, const std::string& currencyType) {
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法获取余额：数据库未连接。");
        return std::nullopt;
    }

    // --- 构建 SQL 查询语句 (注意：不安全的字符串拼接！) ---
    // 理想情况下应使用参数化查询，这需要扩展 IDatabaseConnection 接口
    std::string sql = "SELECT amount FROM player_balances WHERE uuid = '";
    // TODO: 需要对 uuid 和 currencyType 进行 SQL 转义以防止注入！
    // 这是一个简化版本，实际应用中必须转义。
    // 简单的替换 ' 为 '' (SQLite 转义)
    std::string escaped_uuid = uuid;
    size_t pos = escaped_uuid.find('\'');
    while( pos != std::string::npos) {
        escaped_uuid.replace(pos, 1, "''");
        pos = escaped_uuid.find('\'', pos + 2);
    }
    std::string escaped_currencyType = currencyType;
    pos = escaped_currencyType.find('\'');
     while( pos != std::string::npos) {
        escaped_currencyType.replace(pos, 1, "''");
        pos = escaped_currencyType.find('\'', pos + 2);
    }

    sql += escaped_uuid;
    sql += "' AND currency_type = '";
    sql += escaped_currencyType;
    sql += "';";
    mLogger.debug("Executing SQL for getPlayerBalance: {}", sql); // 记录 SQL (调试)
    // --- SQL 构建结束 ---

    try {
        db::DbResult result = mDbConnection.query(sql);

        if (result.empty()) {
            // 没有找到记录，账户不存在
            return std::nullopt;
        }

        if (result.size() > 1) {
            // 理论上不应发生，因为 (uuid, currency_type) 是唯一的
            mLogger.warn("为 UUID: {}, Currency: {} 找到多条余额记录，将使用第一条。", uuid, currencyType);
        }

        const db::DbRow& row = result[0]; // 获取第一行
        if (row.empty()) {
            mLogger.error("查询余额返回了空行。UUID: {}, Currency: {}", uuid, currencyType);
            return std::nullopt;
        }

        // 获取 amount 列 (假设是第一列，索引为 0)
        const db::DbValue& amountValue = row[0];

        // 检查值的类型并提取
        if (std::holds_alternative<int64_t>(amountValue)) {
            // 直接是 int64_t (可能来自 SQLite)
            return std::get<int64_t>(amountValue);
        } else if (std::holds_alternative<std::string>(amountValue)) {
            // 是字符串 (可能来自 MySQL)，尝试转换
            const std::string& amountStr = std::get<std::string>(amountValue);
            try {
                return std::stoll(amountStr); // 尝试转换为 int64_t
            } catch (const std::invalid_argument& e) {
                mLogger.error("无法将数据库返回的余额字符串 '{}' 转换为整数 (invalid_argument)。UUID: {}, Currency: {}", amountStr, uuid, currencyType);
                return std::nullopt;
            } catch (const std::out_of_range& e) {
                mLogger.error("数据库返回的余额字符串 '{}' 超出 int64_t 范围 (out_of_range)。UUID: {}, Currency: {}", amountStr, uuid, currencyType);
                return std::nullopt;
            }
        } else if (std::holds_alternative<std::nullptr_t>(amountValue)) {
             // 理论上不应该发生 (NOT NULL DEFAULT 0)，但处理一下
             mLogger.warn("为 UUID: {}, Currency: {} 获取到 NULL 余额 (应为 0)", uuid, currencyType);
             return 0LL; // 将 NULL 视为 0
        } else {
            // 其他未预期的类型
            mLogger.error("查询余额返回了非预期的类型。UUID: {}, Currency: {}", uuid, currencyType);
             try {
                 // 尝试打印值的类型
                 std::visit([this](auto&& arg){ mLogger.error(" - 实际类型: {}", typeid(arg).name()); }, amountValue);
             } catch(...) {} // 忽略打印类型时的错误
            return std::nullopt;
        }

    } catch (const db::DatabaseException& e) {
        mLogger.error("查询余额时发生数据库错误: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        mLogger.error("查询余额时发生意外错误: {}", e.what());
        return std::nullopt;
    }
}


// 更新：初始化账户的私有辅助函数实现 (从 double 转换)
std::optional<int64_t> MoneyManager::initializeAccount(const std::string& uuid, const std::string& currencyType) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法初始化账户：货币类型 '{}' 未在配置中定义。", currencyType);
        return std::nullopt;
    }

    // 1. 从配置中获取初始余额 (double)
    double initialAmountDouble = 0.0; // 默认初始值为 0.0
    auto it = mConfig.economy.find(currencyType);
    if (it != mConfig.economy.end()) {
        initialAmountDouble = it->second.initialBalance;
    } else {
        mLogger.warn("未在配置的 'economy' 部分找到货币类型 '{}' 的设置，将使用默认初始余额 0.0。", currencyType);
    }

    // 2. 转换初始余额为 int64_t
    std::optional<int64_t> initialAmountIntOpt = convertDoubleToInt64(initialAmountDouble, "initialBalance for " + currencyType);
    if (!initialAmountIntOpt.has_value()) {
        mLogger.error("无法转换配置中货币类型 '{}' 的 initialBalance ({})，初始化账户失败。", currencyType, initialAmountDouble);
        return std::nullopt; // 转换失败
    }
    int64_t initialAmountInt = initialAmountIntOpt.value();

    // 3. 使用 setPlayerBalance 插入新记录 (传入转换后的 int64_t 值)
    mLogger.info("为 UUID: {}, Currency: {} 初始化账户，初始余额: {}", uuid, currencyType, formatBalance(initialAmountInt));
    // 调用 setPlayerBalance 时，它内部会再次检查最低余额（现在 getMinimumBalance 会返回转换后的 int64_t）
    // 注意：setPlayerBalance 也需要被重构以使用接口
    if (setPlayerBalance(uuid, currencyType, initialAmountInt)) { // 传入 int64_t
        return initialAmountInt; // 初始化成功，返回转换后的初始金额
    } else {
        mLogger.error("为 UUID: {}, Currency: {} 初始化账户失败 (setPlayerBalance 调用失败)。", uuid, currencyType);
        return std::nullopt; // 初始化失败
    }
}

// 新增：获取余额或初始化的实现
int64_t MoneyManager::getPlayerBalanceOrInit(const std::string& uuid, const std::string& currencyType) {
    // 1. 尝试获取现有余额
    std::optional<int64_t> currentBalanceOpt = getPlayerBalance(uuid, currencyType);

    if (currentBalanceOpt.has_value()) {
        // 账户已存在，直接返回余额
        return currentBalanceOpt.value();
    } else {
        // 账户不存在，尝试初始化
        std::optional<int64_t> initialBalanceOpt = initializeAccount(uuid, currencyType);
        if (initialBalanceOpt.has_value()) {
            // 初始化成功，返回初始余额
            return initialBalanceOpt.value();
        } else {
            // 初始化失败
            mLogger.error("无法获取或初始化 UUID: {}, Currency: {} 的余额。", uuid, currencyType);
            // 抛出异常或返回一个错误指示值，这里选择抛出异常
            throw std::runtime_error("无法获取或初始化玩家余额");
            // 或者返回 0 并记录错误:
            // return 0LL;
        }
    }
}

// 新增：记录经济交易流水的实现 - 需要重构
bool MoneyManager::logTransaction(
    const std::string& uuid,
    const std::string& currencyType,
    int64_t            changeAmount,
    int64_t            previousAmount,
    const std::string& reason1,
    const std::string& reason2,
    const std::string& reason3
) {
    // 如果变动为 0，可以选择不记录日志
    if (changeAmount == 0) {
        return true; // 视为成功，但不记录
    }

    if (!mDbConnection.isConnected()) {
        mLogger.error("无法记录流水：数据库未连接。");
        return false;
    }

    // --- 构建 SQL (不安全的字符串拼接) ---
    // TODO: 实现参数化查询或安全的字符串转义
    std::string sql = "INSERT INTO economy_log (uuid, currency_type, change_amount, previous_amount, reason1, reason2, reason3) VALUES ('";
    // 简单的 ' 替换转义
    auto escape = [](const std::string& s) -> std::string {
        std::string escaped = s;
        size_t pos = escaped.find('\'');
        while( pos != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos = escaped.find('\'', pos + 2);
        }
        return escaped;
    };

    sql += escape(uuid) + "', '";
    sql += escape(currencyType) + "', ";
    sql += std::to_string(changeAmount) + ", ";
    sql += std::to_string(previousAmount) + ", ";
    sql += (reason1.empty() ? "NULL" : "'" + escape(reason1) + "'") + ", ";
    sql += (reason2.empty() ? "NULL" : "'" + escape(reason2) + "'") + ", ";
    sql += (reason3.empty() ? "NULL" : "'" + escape(reason3) + "'") + ");";
    mLogger.debug("Executing SQL for logTransaction: {}", sql);
    // --- SQL 构建结束 ---

    try {
        int affectedRows = mDbConnection.execute(sql);
        // SQLite 的 execute 通常不直接返回影响的行数，这里假设成功执行即可
        // 可以通过 sqlite3_changes() 获取，但这需要修改接口或实现细节
        mLogger.debug("成功记录流水: UUID={}, Type={}, Change={}, Prev={}, R1={}, R2={}, R3={}",
                      uuid, currencyType, formatBalance(changeAmount), formatBalance(previousAmount),
                      reason1.empty() ? "NULL" : reason1,
                      reason2.empty() ? "NULL" : reason2,
                      reason3.empty() ? "NULL" : reason3);
        return true; // 假设 execute 没抛异常就算成功
    } catch (const db::DatabaseException& e) {
        mLogger.error("记录流水时发生数据库错误: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        mLogger.error("记录流水时发生意外错误: {}", e.what());
        return false;
    }
}


// 设置玩家余额的实现 (更新) - 需要重构
bool MoneyManager::setPlayerBalance(
    const std::string& uuid,
    const std::string& currencyType,
    int64_t            amount,
    const std::string& reason1,
    const std::string& reason2,
    const std::string& reason3
) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法设置余额：货币类型 '{}' 未在配置中定义。", currencyType);
        return false;
    }

    // 新增：检查设置金额是否低于最低值
    int64_t minBalance = getMinimumBalance(currencyType);
    if (amount < minBalance) {
        mLogger.error(
            "无法设置余额：尝试为 UUID: {}, Currency: {} 设置金额 {}，低于最低允许值 {}",
            uuid,
            currencyType,
            formatBalance(amount),
            formatBalance(minBalance)
        );
        return false;
    }

    // 检查数据库连接
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法设置余额：数据库未连接。");
        return false;
    }

    // --- 获取操作前的余额，用于记录流水 ---
    int64_t previousBalance = 0; // 默认为 0
    std::optional<int64_t> previousBalanceOpt = getPlayerBalance(uuid, currencyType);
    if (previousBalanceOpt.has_value()) {
        previousBalance = previousBalanceOpt.value();
    } else {
        // 如果账户不存在，尝试获取配置的初始值作为“之前”的值
        auto it = mConfig.economy.find(currencyType);
        if (it != mConfig.economy.end()) {
            std::optional<int64_t> initialBalanceIntOpt = convertDoubleToInt64(
                it->second.initialBalance,
                "initialBalance for " + currencyType + " (in setPlayerBalance fallback)"
            );
            if (initialBalanceIntOpt.has_value()) {
                previousBalance = initialBalanceIntOpt.value();
            } else {
                mLogger.error(
                    "无法转换配置中货币类型 '{}' 的 initialBalance ({}) 作为 setPlayerBalance 的 previousBalance 回退值。",
                    currencyType,
                    it->second.initialBalance
                );
            }
        }
    }
    // --- 获取操作前余额结束 ---

    // --- 执行数据库更新 (根据数据库类型选择语法) ---
    // TODO: 实现参数化查询或安全的字符串转义
    std::string sql;
    std::string dbType = mDbConnection.getDbType();
    auto escape = [](const std::string& s) -> std::string {
        std::string escaped = s;
        size_t pos = escaped.find('\'');
        while( pos != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos = escaped.find('\'', pos + 2);
        }
        return escaped;
    };

    std::string escaped_uuid = escape(uuid);
    std::string escaped_currencyType = escape(currencyType);
    std::string amount_str = std::to_string(amount);

    if (dbType == "sqlite") {
        sql = "INSERT OR REPLACE INTO player_balances (uuid, currency_type, amount) VALUES ('";
        sql += escaped_uuid + "', '";
        sql += escaped_currencyType + "', ";
        sql += amount_str + ");";
    } else if (dbType == "mysql") {
        // 使用 INSERT ... ON DUPLICATE KEY UPDATE
        sql = "INSERT INTO player_balances (uuid, currency_type, amount) VALUES ('";
        sql += escaped_uuid + "', '";
        sql += escaped_currencyType + "', ";
        sql += amount_str + ")";
        sql += " ON DUPLICATE KEY UPDATE amount = VALUES(amount);";
        // 或者使用 REPLACE INTO (效果类似，但实现不同，会先 DELETE 再 INSERT)
        // sql = "REPLACE INTO player_balances (uuid, currency_type, amount) VALUES ('";
        // sql += escaped_uuid + "', '";
        // sql += escaped_currencyType + "', ";
        // sql += amount_str + ");";
    } else {
        mLogger.error("不支持的数据库类型 '{}'，无法执行 setPlayerBalance。", dbType);
        return false;
    }

    mLogger.debug("Executing SQL for setPlayerBalance ({}): {}", dbType, sql);
    // --- SQL 构建结束 ---

    try {
        mDbConnection.execute(sql);
        bool dbUpdateSucceeded = true; // 假设 execute 没抛异常就算成功

        // --- 记录流水 ---
        int64_t changeAmount = amount - previousBalance;
        if (changeAmount != 0) {
             if (!logTransaction(uuid, currencyType, changeAmount, previousBalance, reason1, reason2, reason3)) {
                 mLogger.error("数据库余额已更新，但记录流水失败！UUID: {}, Currency: {}", uuid, currencyType);
                 // 根据策略决定是否返回 false
                 // return false;
             }
        } else {
             mLogger.debug("Set 操作未改变余额，不记录流水。UUID: {}, Currency: {}", uuid, currencyType);
        }
        mLogger.debug("成功设置/更新 UUID: {}, Currency: {} 的余额为: {}", uuid, currencyType, formatBalance(amount));
        return true;

    } catch (const db::DatabaseException& e) {
        mLogger.error("设置余额时发生数据库错误: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        mLogger.error("设置余额时发生意外错误: {}", e.what());
        return false;
    }
}


// 增加玩家余额的实现 (更新) - 需要重构
bool MoneyManager::addPlayerBalance(
    const std::string& uuid,
    const std::string& currencyType,
    int64_t            amountToAdd,
    const std::string& reason1,
    const std::string& reason2,
    const std::string& reason3
) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法增加余额：货币类型 '{}' 未在配置中定义。", currencyType);
        return false;
    }

    // 检查增加的金额是否为正数
    if (amountToAdd <= 0) {
        mLogger.warn("尝试为 UUID: {}, Currency: {} 增加非正数金额 ({})", uuid, currencyType, formatBalance(amountToAdd));
        return amountToAdd == 0; // 增加 0 视为成功，但不记录流水
    }

    // --- 事务处理 (SQLite 默认每个语句是一个事务，除非显式 BEGIN/COMMIT) ---
    // 可以考虑显式包裹事务以保证原子性

    try {
        // 1. 获取当前余额，如果不存在则初始化
        int64_t currentBalance = getPlayerBalanceOrInit(uuid, currencyType);

        // 2. 检查溢出
        if (currentBalance > std::numeric_limits<int64_t>::max() - amountToAdd) {
             mLogger.error("增加余额时检测到潜在溢出。UUID: {}, Currency: {}", uuid, currencyType);
             return false;
        }
        int64_t newBalance = currentBalance + amountToAdd;

        // 3. 设置新余额 (直接执行数据库更新)
        // TODO: 实现参数化查询或安全的字符串转义
        std::string sql = "UPDATE player_balances SET amount = ";
        sql += std::to_string(newBalance);
        sql += " WHERE uuid = '";
        auto escape = [](const std::string& s) -> std::string {
            std::string escaped = s;
            size_t pos = escaped.find('\'');
            while( pos != std::string::npos) {
                escaped.replace(pos, 1, "''");
                pos = escaped.find('\'', pos + 2);
            }
            return escaped;
        };
        sql += escape(uuid) + "' AND currency_type = '";
        sql += escape(currencyType) + "';";
        mLogger.debug("Executing SQL for addPlayerBalance: {}", sql);

        mDbConnection.execute(sql); // 执行更新

        // 4. 记录流水
        if (!logTransaction(uuid, currencyType, amountToAdd, currentBalance, reason1, reason2, reason3)) {
            mLogger.error("数据库余额已更新，但记录流水失败！(增加余额) UUID: {}, Currency: {}", uuid, currencyType);
            // 根据策略决定是否回滚或返回失败
            // return false;
        }
        mLogger.debug("成功为 UUID: {}, Currency: {} 增加余额 {}, 新余额: {}", uuid, currencyType, formatBalance(amountToAdd), formatBalance(newBalance));
        return true; // 数据库更新和流水记录（或尝试记录）都完成

    } catch (const db::DatabaseException& e) {
         mLogger.error("增加余额时发生数据库错误: {}", e.what());
         return false;
    } catch (const std::runtime_error& e) { // Catch getPlayerBalanceOrInit exception
        mLogger.error("增加余额失败 (获取或初始化时出错): {}", e.what());
        return false;
    } catch (const std::exception& e) {
        mLogger.error("增加余额时发生意外错误: {}", e.what());
        return false;
    }
}

// 减少玩家余额的实现 (更新) - 需要重构
bool MoneyManager::subtractPlayerBalance(
    const std::string& uuid,
    const std::string& currencyType,
    int64_t            amountToSubtract,
    const std::string& reason1,
    const std::string& reason2,
    const std::string& reason3
) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法减少余额：货币类型 '{}' 未在配置中定义。", currencyType);
        return false;
    }

    // 检查减少的金额是否为正数
    if (amountToSubtract <= 0) {
        mLogger.warn("尝试为 UUID: {}, Currency: {} 减少非正数金额 ({})", uuid, currencyType, formatBalance(amountToSubtract));
        return amountToSubtract == 0; // 减少 0 视为成功，不记录流水
    }

    // --- 事务处理 ---

    try {
        // 1. 获取当前余额 (不初始化)
        std::optional<int64_t> currentBalanceOpt = getPlayerBalance(uuid, currencyType);

        if (!currentBalanceOpt.has_value()) {
            mLogger.warn("尝试从不存在的账户扣款。UUID: {}, Currency: {}", uuid, currencyType);
            return false;
        }
        int64_t currentBalance = currentBalanceOpt.value();

        // 2. 检查余额是否足够 (原始检查)
        if (currentBalance < amountToSubtract) {
            mLogger.warn("余额不足无法扣款。UUID: {}, Currency: {}, 当前: {}, 请求: {}",
                         uuid, currencyType, formatBalance(currentBalance), formatBalance(amountToSubtract));
            return false;
        }

        // 3. 检查下溢
         if (currentBalance < std::numeric_limits<int64_t>::min() + amountToSubtract) {
             mLogger.error("减少余额时检测到潜在下溢。UUID: {}, Currency: {}", uuid, currencyType);
             return false;
         }
        int64_t newBalance = currentBalance - amountToSubtract;

        // 新增：检查扣款后是否低于最低余额
        int64_t minBalance = getMinimumBalance(currencyType);
        if (newBalance < minBalance) {
            mLogger.error(
                "无法减少余额：操作将使 UUID: {} 的 Currency: {} 余额 ({}) 低于最低允许值 ({})",
                uuid,
                currencyType,
                formatBalance(newBalance),
                formatBalance(minBalance)
            );
            return false;
        }


        // 4. 设置新余额 (直接执行数据库更新)
        // TODO: 实现参数化查询或安全的字符串转义
        std::string sql = "UPDATE player_balances SET amount = ";
        sql += std::to_string(newBalance);
        sql += " WHERE uuid = '";
         auto escape = [](const std::string& s) -> std::string {
            std::string escaped = s;
            size_t pos = escaped.find('\'');
            while( pos != std::string::npos) {
                escaped.replace(pos, 1, "''");
                pos = escaped.find('\'', pos + 2);
            }
            return escaped;
        };
        sql += escape(uuid) + "' AND currency_type = '";
        sql += escape(currencyType) + "';";
         mLogger.debug("Executing SQL for subtractPlayerBalance: {}", sql);

        mDbConnection.execute(sql); // 执行更新

        // 5. 记录流水
        // 注意：changeAmount 是负数
        if (!logTransaction(uuid, currencyType, -amountToSubtract, currentBalance, reason1, reason2, reason3)) {
            mLogger.error("数据库余额已更新，但记录流水失败！(减少余额) UUID: {}, Currency: {}", uuid, currencyType);
            // 根据策略决定是否回滚或返回失败
            // return false;
        }
         mLogger.debug("成功为 UUID: {}, Currency: {} 减少余额 {}, 新余额: {}", uuid, currencyType, formatBalance(amountToSubtract), formatBalance(newBalance));
        return true;

    } catch (const db::DatabaseException& e) {
         mLogger.error("减少余额时发生数据库错误: {}", e.what());
         return false;
    } catch (const std::exception& e) {
        mLogger.error("减少余额时发生意外错误: {}", e.what());
        return false;
    }
}


// --- 格式化与解析辅助函数 ---

// 将整数余额 (乘以 100) 格式化为带两位小数的字符串
std::string MoneyManager::formatBalance(int64_t amount) {
    // 处理 0 的特殊情况
    if (amount == 0) {
        return "0.00";
    }
    std::stringstream ss; // 使用字符串流进行格式化
    bool isNegative = amount < 0; // 检查是否为负数

    // 处理 int64_t 的最小值，因为 abs(int64_t::min()) 会溢出
    int64_t absAmount;
    if (amount == std::numeric_limits<int64_t>::min()) {
        // 直接返回其字符串表示形式 "-92233720368547758.08"
        return "-92233720368547758.08";
    } else {
        absAmount = std::abs(amount); // 获取绝对值 (对于非最小值是安全的)
    }

    int64_t integerPart = absAmount / 100; // 计算整数部分 (元)
    int64_t fractionalPart = absAmount % 100; // 计算小数部分 (分)

    // 如果是负数，先添加负号
    if (isNegative) {
        ss << "-";
    }
    // 输出整数部分，然后是小数点
    ss << integerPart << ".";
    // 输出两位小数部分，使用 std::setw(2) 和 std::setfill('0') 确保总是两位，不足则补零
    ss << std::setw(2) << std::setfill('0') << fractionalPart;
    return ss.str(); // 返回格式化后的字符串
}

// 将带小数的字符串金额解析为整数余额 (乘以 100)
std::optional<int64_t> MoneyManager::parseBalance(const std::string& formattedAmount) {
    std::string amountStr = formattedAmount; // 复制输入字符串以便修改
    bool isNegative = false; // 标记是否为负数

    //  清理字符串前后的空白字符
    // amountStr.erase(0, amountStr.find_first_not_of(" \t\n\r\f\v"));
    // amountStr.erase(amountStr.find_last_not_of(" \t\n\r\f\v") + 1);

    // 检查清理后是否为空字符串
    if (amountStr.empty()) {
        return std::nullopt; // 无效输入
    }

    // 处理可能存在的负号
    if (amountStr[0] == '-') {
        isNegative = true;
        amountStr = amountStr.substr(1); // 移除负号，处理剩余部分
         if (amountStr.empty()) return std::nullopt; // 只有一个负号是无效的
    }

    // 查找小数点的位置
    size_t decimalPos = amountStr.find('.');
    int64_t integerPart = 0;    // 存储整数部分的值
    int64_t fractionalPart = 0; // 存储小数部分的值 (分)
    std::string integerStr;     // 存储整数部分的字符串
    std::string fractionalStr;  // 存储小数部分的字符串

    if (decimalPos == std::string::npos) {
        // 没有小数点，整个字符串都是整数部分
        integerStr = amountStr;
        fractionalStr = "00"; // 小数部分默认为 00
    } else {
        // 有小数点
        integerStr = amountStr.substr(0, decimalPos); // 获取小数点前的整数部分
        fractionalStr = amountStr.substr(decimalPos + 1); // 获取小数点后的部分

        // --- 验证和处理小数部分 ---
        if (fractionalStr.length() > 2) {
            // 小数部分超过两位，视为无效格式 (严格模式)
             // 或者可以选择截断：fractionalStr = fractionalStr.substr(0, 2);
             return std::nullopt; // 返回空 optional 表示格式错误
        } else if (fractionalStr.length() == 1) {
            fractionalStr += '0'; // 如果只有一位小数，补齐 '0'，例如 "12.3" -> "30"
        } else if (fractionalStr.empty()) {
            fractionalStr = "00"; // 处理 "12." 的情况，小数部分视为 "00"
        }

        // 检查小数部分是否只包含数字
        if (fractionalStr.find_first_not_of("0123456789") != std::string::npos) {
            return std::nullopt; // 包含非数字字符，无效
        }
        // --- 小数部分处理完毕 ---
    }

     // --- 验证和处理整数部分 ---
     // 处理整数部分为空的情况 (例如 ".50" 或 ".")
     if (integerStr.empty()) {
         if (decimalPos != std::string::npos && !fractionalStr.empty()) {
             // 情况如 ".50"，整数部分视为 0
             integerStr = "0";
         } else {
             // 情况如 "." 或 "" (如果原始字符串就是 ".")
             return std::nullopt; // 无效格式
         }
     }

     // 检查整数部分是否只包含数字
     if (integerStr.find_first_not_of("0123456789") != std::string::npos) {
         return std::nullopt; // 包含非数字字符，无效
     }
     // --- 整数部分处理完毕 ---

    // --- 将字符串部分转换为整数，并进行溢出检查 ---
    try {
        // 使用 std::stoll 将字符串转换为 long long (int64_t)
        // 注意：stoll 会自动处理前导零
        integerPart = std::stoll(integerStr);
        fractionalPart = std::stoll(fractionalStr); // 小数部分已验证并补齐为两位
    } catch (const std::invalid_argument&) {
        // stoll 无法转换 (理论上不应发生，因为已做数字检查)
         return std::nullopt; // 无效参数异常
    } catch (const std::out_of_range&) {
        // 转换结果超出了 long long 的范围
         return std::nullopt; // 超出范围异常
     }

    // --- 合并整数和小数部分 (乘以 100)，并进行溢出检查 ---
    int64_t integerCents = 0;
    // 计算 integerPart * 100 的安全上限，防止溢出
    const int64_t max_int_div_100 = std::numeric_limits<int64_t>::max() / 100;
    const int64_t max_int_mod_100 = std::numeric_limits<int64_t>::max() % 100;

    // 检查乘以 100 是否会溢出
    if (integerPart > max_int_div_100 ||
       (integerPart == max_int_div_100 && fractionalPart > max_int_mod_100)) {
          // 正向溢出
          // 注意：这里没有使用 mLogger，因为这是静态方法
          return std::nullopt;
     }
     integerCents = integerPart * 100; // 安全地乘以 100

    // 检查加上小数部分是否会溢出 (理论上 fractionalPart < 100，不太可能在此处溢出)
    // if (integerCents > std::numeric_limits<int64_t>::max() - fractionalPart) {
    //     return std::nullopt; // Overflow
    // }
    int64_t totalCents = integerCents + fractionalPart; // 合并为总的分数

    // --- 处理负号 ---
    if (isNegative) {
        // 处理负零 "-0.00" 或 "-0"
        if (totalCents == 0) {
            return 0LL; // 返回 0
        }
        // 对于非零值，直接取反
        // 能通过正数检查的最大值是 int64_t::max()，其相反数 -int64_t::max() 是有效的。
        return -totalCents;
    } else {
        // 返回正值
        return totalCents;
    }
}

// --- 查询流水实现 --- - 需要重构
std::vector<TransactionLogEntry> MoneyManager::queryTransactionLogs(
    const std::optional<std::string>& uuidFilter,
    const std::optional<std::string>& currencyTypeFilter,
    const std::optional<std::string>& startTimeFilter,
    const std::optional<std::string>& endTimeFilter,
    const std::optional<std::string>& reason1Filter,
    const std::optional<std::string>& reason2Filter,
    const std::optional<std::string>& reason3Filter,
    size_t                            limit,
    size_t                            offset,
    bool                              ascendingOrder
) {
    std::vector<TransactionLogEntry> results; // 用于存储结果

    if (!mDbConnection.isConnected()) {
        mLogger.error("无法查询流水：数据库未连接。");
        return results; // 返回空列表
    }

    // --- 动态构建 SQL 查询 (不安全的字符串拼接) ---
    // TODO: 实现参数化查询或安全的字符串转义
    std::stringstream sqlBuilder;
    sqlBuilder << "SELECT id, timestamp, uuid, currency_type, change_amount, previous_amount, reason1, reason2, reason3 FROM economy_log";

    std::vector<std::string> whereClauses;
    auto escape = [](const std::string& s) -> std::string {
        std::string escaped = s;
        size_t pos = escaped.find('\'');
        while( pos != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos = escaped.find('\'', pos + 2);
        }
        return escaped;
    };

    if (uuidFilter.has_value() && !uuidFilter.value().empty()) {
        whereClauses.push_back("uuid = '" + escape(uuidFilter.value()) + "'");
    }
    if (currencyTypeFilter.has_value() && !currencyTypeFilter.value().empty()) {
        whereClauses.push_back("currency_type = '" + escape(currencyTypeFilter.value()) + "'");
    }
    if (startTimeFilter.has_value() && !startTimeFilter.value().empty()) {
        // 注意：日期时间格式可能需要适配数据库
        whereClauses.push_back("timestamp >= '" + escape(startTimeFilter.value()) + "'");
    }
    if (endTimeFilter.has_value() && !endTimeFilter.value().empty()) {
        whereClauses.push_back("timestamp <= '" + escape(endTimeFilter.value()) + "'");
    }
     if (reason1Filter.has_value() && !reason1Filter.value().empty()) {
        whereClauses.push_back("reason1 LIKE '%" + escape(reason1Filter.value()) + "%'");
    }
    if (reason2Filter.has_value() && !reason2Filter.value().empty()) {
        whereClauses.push_back("reason2 LIKE '%" + escape(reason2Filter.value()) + "%'");
    }
    if (reason3Filter.has_value() && !reason3Filter.value().empty()) {
        whereClauses.push_back("reason3 LIKE '%" + escape(reason3Filter.value()) + "%'");
    }


    if (!whereClauses.empty()) {
        sqlBuilder << " WHERE ";
        for (size_t i = 0; i < whereClauses.size(); ++i) {
            sqlBuilder << whereClauses[i] << (i < whereClauses.size() - 1 ? " AND " : "");
        }
    }

    sqlBuilder << " ORDER BY timestamp " << (ascendingOrder ? "ASC" : "DESC");

    if (limit > 0) {
        sqlBuilder << " LIMIT " << limit;
        if (offset > 0) {
            sqlBuilder << " OFFSET " << offset;
        }
    }
    sqlBuilder << ";";

    std::string finalSql = sqlBuilder.str();
    mLogger.debug("Executing SQL for queryTransactionLogs: {}", finalSql);

    try {
        db::DbResult queryResult = mDbConnection.query(finalSql);

        results.reserve(queryResult.size()); // 预分配空间

        for (const auto& row : queryResult) {
            if (row.size() != 9) { // 期望 9 列
                mLogger.error("查询流水返回了列数不匹配的行 (预期 9, 实际 {})", row.size());
                continue; // 跳过此行
            }

            TransactionLogEntry entry;
            try {
                // Helper lambda to safely get int64_t from DbValue (handles string conversion)
                auto getInt64Value = [&](const db::DbValue& val, const std::string& colName) -> int64_t {
                    if (std::holds_alternative<int64_t>(val)) {
                        return std::get<int64_t>(val);
                    } else if (std::holds_alternative<std::string>(val)) {
                        const std::string& strVal = std::get<std::string>(val);
                        try {
                            return std::stoll(strVal);
                        } catch (...) {
                            mLogger.error("无法将流水列 '{}' 的字符串值 '{}' 转换为 int64_t。", colName, strVal);
                            return 0LL; // 返回默认值或抛出异常
                        }
                    } else if (std::holds_alternative<std::nullptr_t>(val)) {
                         mLogger.warn("流水列 '{}' 返回了 NULL 值 (预期为数值)。", colName);
                         return 0LL;
                    } else {
                        mLogger.error("流水列 '{}' 返回了非预期的类型。", colName);
                        return 0LL;
                    }
                };

                 // Helper lambda to safely get string from DbValue (handles nullptr)
                auto getStringValue = [&](const db::DbValue& val, const std::string& colName) -> std::string {
                    if (std::holds_alternative<std::string>(val)) {
                        return std::get<std::string>(val);
                    } else if (std::holds_alternative<std::nullptr_t>(val)) {
                        return ""; // Treat NULL as empty string
                    } else {
                         mLogger.error("流水列 '{}' 返回了非预期的类型 (预期为字符串或 NULL)。", colName);
                         return "";
                    }
                };


                // 使用辅助函数提取数据
                entry.id = getInt64Value(row[0], "id");
                entry.timestamp = getStringValue(row[1], "timestamp"); // timestamp 通常是字符串
                entry.uuid = getStringValue(row[2], "uuid");
                entry.currencyType = getStringValue(row[3], "currency_type");
                // 从数据库获取 int64_t (分)，然后转换为 double (元) 存储在结构体中
                entry.changeAmount = static_cast<double>(getInt64Value(row[4], "change_amount")) / 100.0;
                entry.previousAmount = static_cast<double>(getInt64Value(row[5], "previous_amount")) / 100.0;
                entry.reason1 = getStringValue(row[6], "reason1");
                entry.reason2 = getStringValue(row[7], "reason2");
                entry.reason3 = getStringValue(row[8], "reason3");

                results.push_back(std::move(entry));
            } catch (const std::exception& e) { // Catch broader exceptions during processing
                 mLogger.error("处理流水记录时发生错误: {}", e.what());
            }
        }

    } catch (const db::DatabaseException& e) {
        mLogger.error("查询流水时发生数据库错误: {}", e.what());
        // 可以选择抛出异常或返回空列表
        // throw std::runtime_error("查询流水失败");
    } catch (const std::exception& e) {
        mLogger.error("查询流水时发生意外错误: {}", e.what());
    }

    return results;
}


} // namespace czmoney
