#include "czmoney/money.h"
#include "czmoney/mysql.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/memory/Hook.h"
#include <mysql.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <utility> // For std::move

namespace czmoney {

// RAII (Resource Acquisition Is Initialization) 辅助类，用于自动管理 MYSQL_STMT 的生命周期
// 确保在作用域结束时自动调用 mysql_stmt_close
class StatementGuard {
    MYSQL_STMT* mStmt; // 指向 MySQL 语句句柄的指针
public:
    // 构造函数：获取语句句柄
    explicit StatementGuard(MYSQL_STMT* stmt) : mStmt(stmt) {}
    // 析构函数：如果句柄有效，则关闭它
    ~StatementGuard() {
        if (mStmt) {
            mysql_stmt_close(mStmt);
        }
    }
    // 禁用拷贝构造函数
    StatementGuard(const StatementGuard&) = delete;
    // 禁用拷贝赋值运算符
    StatementGuard& operator=(const StatementGuard&) = delete;
    // 允许移动构造函数
    StatementGuard(StatementGuard&& other) noexcept : mStmt(other.mStmt) {
        other.mStmt = nullptr; // 将源对象的指针置空，防止重复释放
    }
    // 允许移动赋值运算符
    StatementGuard& operator=(StatementGuard&& other) noexcept {
        if (this != &other) { // 防止自赋值
            if (mStmt) mysql_stmt_close(mStmt); // 释放当前持有的资源
            mStmt = other.mStmt; // 获取源对象的资源
            other.mStmt = nullptr; // 将源对象的指针置空
        }
        return *this;
    }
    // 获取原始的语句句柄指针
    MYSQL_STMT* get() const { return mStmt; }
};

// 辅助类，用于管理 MYSQL_BIND 结构体数组
// 注意：这个类本身不负责 MYSQL_BIND 结构体内部指针指向内存的生命周期管理
// 它主要用于方便地构建和获取绑定参数数组
class BindGuard {
    std::vector<MYSQL_BIND> mBinds; // 使用 std::vector 存储 MYSQL_BIND 结构体
public:
    BindGuard() = default; // 默认构造函数
    ~BindGuard() = default; // 默认析构函数，vector 会自动管理其元素的生命周期

    // 添加一个 MYSQL_BIND 结构体到数组中
    void add(const MYSQL_BIND& bind) {
        mBinds.push_back(bind);
    }
    // 获取指向 MYSQL_BIND 数组起始位置的指针
    // 如果数组为空，返回 nullptr
    MYSQL_BIND* get() {
        return mBinds.empty() ? nullptr : mBinds.data();
    }
    // 获取数组中 MYSQL_BIND 结构体的数量
    size_t count() const {
        return mBinds.size();
    }
    // 清空数组
     void clear() {
        mBinds.clear();
    }
};

// --- 私有辅助函数实现 ---

// 检查货币类型是否已配置
bool MoneyManager::isCurrencyConfigured(const std::string& currencyType) const {
    return mConfig.economy.count(currencyType) > 0;
}


// MoneyManager 构造函数实现 (更新)
MoneyManager::MoneyManager(db::MySQLConnection& dbConn, const Config& config) : // 添加 config 参数
    mDbConnection(dbConn), // 初始化数据库连接引用成员
    mConfig(config),       // 初始化配置引用成员 (新增)
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
    // 检查数据库连接
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法初始化货币表：数据库未连接。"); // Log error if not connected
        return false;
    }

    // 定义创建表的 SQL 语句 (使用原始字符串字面量 R"(...)" 以方便书写多行 SQL)
    // - IF NOT EXISTS: 确保如果表已存在，则不会报错
    // - id: 自增主键
    // - uuid: 玩家 UUID (VARCHAR(36))
    // - currency_type: 货币类型 (VARCHAR(50))
    // - amount: 存储乘以 100 的整数金额 (BIGINT), 默认为 0
    // - last_updated: 时间戳，记录最后更新时间
    // - UNIQUE KEY unique_player_currency: uuid 和 currency_type 的联合唯一索引，确保数据唯一性
    // - ENGINE=InnoDB: 使用 InnoDB 存储引擎 (支持事务)
    // - DEFAULT CHARSET=utf8mb4: 使用 utf8mb4 字符集 (支持 emoji 等)
    const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS player_balances (
            id INT AUTO_INCREMENT PRIMARY KEY,
            uuid VARCHAR(36) NOT NULL,
            currency_type VARCHAR(50) NOT NULL,
            amount BIGINT NOT NULL DEFAULT 0,
            last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            UNIQUE KEY unique_player_currency (uuid, currency_type)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )";

    // 执行 SQL 查询
    if (mDbConnection.query(createTableSQL) != 0) {
        // 如果查询失败，记录详细错误信息
        mLogger.error("创建或验证 'player_balances' 表失败: {}", mysql_error(mDbConnection.getMYSQL()));
        return false; // 初始化失败
    }

    // 记录成功信息
    mLogger.info("'player_balances' 表初始化成功。");
    return true; // 初始化成功
}

// 检查玩家账户是否存在的实现
bool MoneyManager::hasAccount(const std::string& uuid, const std::string& currencyType) {
     // 通过尝试获取余额并检查结果（std::optional）是否包含值来判断账户是否存在
     return getPlayerBalance(uuid, currencyType).has_value();
}


// 获取玩家余额的实现 (不初始化)
std::optional<int64_t> MoneyManager::getPlayerBalance(const std::string& uuid, const std::string& currencyType) {
    // 检查数据库连接
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法获取余额：数据库未连接。");
        return std::nullopt; // 返回空 optional 表示失败
    }

    MYSQL* mysql = mDbConnection.getMYSQL(); // 获取底层 MySQL 连接指针
    MYSQL_STMT* stmt = mysql_stmt_init(mysql); // 初始化预处理语句句柄
    if (!stmt) {
        mLogger.error("mysql_stmt_init() 失败: {}", mysql_error(mysql));
        return std::nullopt; // 初始化失败
    }
    StatementGuard stmtGuard(stmt); // 使用 RAII 确保语句句柄被关闭

    // 定义 SQL 查询语句，使用 ? 作为参数占位符，提高安全性，防止 SQL 注入
    const std::string sql = "SELECT amount FROM player_balances WHERE uuid = ? AND currency_type = ?";
    // 准备 SQL 语句
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) { // 显式转换长度类型
        mLogger.error("mysql_stmt_prepare() 失败 (获取余额): {}", mysql_stmt_error(stmt));
        return std::nullopt; // 准备失败
    }

    // --- 绑定输入参数 ---
    MYSQL_BIND params[2]; // 创建 MYSQL_BIND 结构体数组用于存储参数信息
    memset(params, 0, sizeof(params)); // 初始化数组为 0，避免未定义行为

    // 绑定第一个参数：uuid (字符串类型)
    unsigned long uuidLen = static_cast<unsigned long>(uuid.length()); // 获取字符串长度
    params[0].buffer_type = MYSQL_TYPE_STRING; // 参数类型：字符串
    params[0].buffer = const_cast<char*>(uuid.c_str()); // 参数值指针 (需要 const_cast)
    params[0].buffer_length = uuidLen; // 缓冲区长度 (对于字符串，通常等于实际长度)
    params[0].length = &uuidLen; // 指向实际数据长度的指针

    // 绑定第二个参数：currencyType (字符串类型)
    unsigned long currencyTypeLen = static_cast<unsigned long>(currencyType.length());
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = const_cast<char*>(currencyType.c_str());
    params[1].buffer_length = currencyTypeLen;
    params[1].length = &currencyTypeLen;

    // 将参数绑定到语句句柄
    if (mysql_stmt_bind_param(stmt, params)) {
        mLogger.error("mysql_stmt_bind_param() 失败 (获取余额): {}", mysql_stmt_error(stmt));
        return std::nullopt; // 绑定失败
    }
    // --- 输入参数绑定结束 ---

    // 执行预处理语句
    if (mysql_stmt_execute(stmt)) {
        mLogger.error("mysql_stmt_execute() 失败 (获取余额): {}", mysql_stmt_error(stmt));
        return std::nullopt; // 执行失败
    }

    // --- 绑定输出结果 ---
    MYSQL_BIND results[1]; // 创建 MYSQL_BIND 结构体数组用于存储结果信息
    memset(results, 0, sizeof(results)); // 初始化
    int64_t balance = 0; // 用于存储查询结果的变量
    // 使用 C++ bool 类型替代 MySQL 的 my_bool
    bool is_null = false; // 用于指示结果是否为 NULL 的标志
    bool error = false;   // 用于指示获取此列时是否发生错误的标志

    // 绑定第一个结果列：amount (长整型)
    results[0].buffer_type = MYSQL_TYPE_LONGLONG; // 结果类型：对应 BIGINT
    results[0].buffer = &balance; // 指向存储结果的变量
    results[0].is_null = &is_null; // 指向 NULL 标志变量
    results[0].error = &error;     // 指向错误标志变量

    // 将结果绑定到语句句柄
    if (mysql_stmt_bind_result(stmt, results)) {
        mLogger.error("mysql_stmt_bind_result() 失败 (获取余额): {}", mysql_stmt_error(stmt));
        return std::nullopt; // 结果绑定失败
    }
    // --- 输出结果绑定结束 ---

    // 获取查询结果 (尝试读取一行)
    int fetch_status = mysql_stmt_fetch(stmt);

    if (fetch_status == 0) { // 成功获取到一行数据
        if (is_null) {
             // 理论上不应该发生，因为表定义了 NOT NULL DEFAULT 0，但以防万一
             mLogger.warn("为 UUID: {}, Currency: {} 获取到 NULL 余额", uuid, currencyType);
             // 将 NULL 视为 0
             return 0LL; // 返回 0
        }
        if (error) {
             // 获取列数据时发生错误
             mLogger.error("获取余额列时出错 UUID: {}, Currency: {}", uuid, currencyType);
             return std::nullopt; // 获取列错误
        }
        // 成功获取到余额
        return balance;
    } else if (fetch_status == MYSQL_NO_DATA) { // 没有找到匹配的行
        // 这表示账户不存在
        return std::nullopt; // 返回空 optional 表示账户不存在
    } else { // 获取数据时发生其他错误 (fetch_status == 1 或 MYSQL_DATA_TRUNCATED)
        mLogger.error("mysql_stmt_fetch() 失败或数据截断 (获取余额): {}", mysql_stmt_error(stmt));
        return std::nullopt; // 获取失败
    }
}

// 新增：初始化账户的私有辅助函数实现
std::optional<int64_t> MoneyManager::initializeAccount(const std::string& uuid, const std::string& currencyType) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法初始化账户：货币类型 '{}' 未在配置中定义。", currencyType);
        return std::nullopt; // 返回空 optional 表示失败
    }

    // 1. 从配置中获取初始余额
    int64_t initialAmount = 0; // 默认初始值为 0
    auto it = mConfig.economy.find(currencyType); // 从新的 economy map 中查找
    if (it != mConfig.economy.end()) {
        // 找到了对应货币类型的配置
        initialAmount = it->second.initialBalance; // 获取 CurrencyConfig 中的 initialBalance
    } else {
        // 未找到特定货币类型的配置，使用默认值 0
        mLogger.warn("未在配置的 'economy' 部分找到货币类型 '{}' 的设置，将使用默认初始余额 0。", currencyType);
        // 注意：这里也可以选择从一个全局默认值读取，如果 Config 结构体未来添加了类似 defaultInitialBalance 的字段
    }

    // 2. 使用 setPlayerBalance 插入新记录
    mLogger.info("为 UUID: {}, Currency: {} 初始化账户，初始余额: {}", uuid, currencyType, formatBalance(initialAmount));
    if (setPlayerBalance(uuid, currencyType, initialAmount)) {
        return initialAmount; // 初始化成功，返回初始金额
    } else {
        mLogger.error("为 UUID: {}, Currency: {} 初始化账户失败。", uuid, currencyType);
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


// 设置玩家余额的实现
bool MoneyManager::setPlayerBalance(const std::string& uuid, const std::string& currencyType, int64_t amount) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法设置余额：货币类型 '{}' 未在配置中定义。", currencyType);
        return false;
    }

    // 检查数据库连接
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法设置余额：数据库未连接。");
        return false; // 连接失败 (修正了重复的 return false)
    }

    MYSQL* mysql = mDbConnection.getMYSQL(); // 获取底层连接
    MYSQL_STMT* stmt = mysql_stmt_init(mysql); // 初始化语句句柄
    if (!stmt) {
        mLogger.error("mysql_stmt_init() 失败: {}", mysql_error(mysql));
        return false; // 初始化失败
    }
    StatementGuard stmtGuard(stmt); // RAII 管理语句生命周期

    // 使用 INSERT ... ON DUPLICATE KEY UPDATE 实现 "upsert"（插入或更新）逻辑
    // 如果 (uuid, currency_type) 的组合键已存在，则更新 amount 字段为新值 (VALUES(amount))
    // 否则，插入新行
    const std::string sql = R"(
        INSERT INTO player_balances (uuid, currency_type, amount)
        VALUES (?, ?, ?)
        ON DUPLICATE KEY UPDATE amount = VALUES(amount);
    )";

    // 准备 SQL 语句
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
        mLogger.error("mysql_stmt_prepare() 失败 (设置余额): {}", mysql_stmt_error(stmt));
        return false; // 准备失败
    }

    // --- 绑定输入参数 ---
    MYSQL_BIND params[3]; // 3 个参数：uuid, currencyType, amount
    memset(params, 0, sizeof(params)); // 初始化

    // 绑定 uuid (字符串)
    unsigned long uuidLen = static_cast<unsigned long>(uuid.length());
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = const_cast<char*>(uuid.c_str());
    params[0].buffer_length = uuidLen;
    params[0].length = &uuidLen;

    // 绑定 currencyType (字符串)
    unsigned long currencyTypeLen = static_cast<unsigned long>(currencyType.length());
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = const_cast<char*>(currencyType.c_str());
    params[1].buffer_length = currencyTypeLen;
    params[1].length = &currencyTypeLen;

    // 绑定 amount (长整型, BIGINT)
    params[2].buffer_type = MYSQL_TYPE_LONGLONG; // 类型
    params[2].buffer = &amount; // 指向 amount 变量的指针
    params[2].is_unsigned = false; // 金额是有符号的 (int64_t)

    // 将参数绑定到语句
    if (mysql_stmt_bind_param(stmt, params)) {
        mLogger.error("mysql_stmt_bind_param() 失败 (设置余额): {}", mysql_stmt_error(stmt));
        return false; // 绑定失败
    }
    // --- 输入参数绑定结束 ---

    // 执行语句
    if (mysql_stmt_execute(stmt)) {
        mLogger.error("mysql_stmt_execute() 失败 (设置余额): {}", mysql_stmt_error(stmt));
        // 这里可以考虑检查具体的错误码，例如处理死锁 (ER_LOCK_DEADLOCK) 等情况
        return false; // 执行失败
    }

    // 检查受影响的行数 (可选，但有助于调试和确认操作结果)
    // INSERT 返回 1, UPDATE 返回 1 (如果值改变) 或 0 (如果值未变), INSERT...ON DUPLICATE KEY UPDATE:
    // - 如果执行了 INSERT，返回 1
    // - 如果执行了 UPDATE 且值改变，返回 2
    // - 如果执行了 UPDATE 但值未变，返回 0 (MySQL 5.1.12 之前可能返回 1)
    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
    if (affected_rows == (my_ulonglong)-1) {
         // 获取受影响行数时出错
         mLogger.error("获取受影响行数时出错 (设置余额) UUID: {}, Currency: {}", uuid, currencyType);
         // 即使这里出错，操作本身可能已经成功，所以不一定返回 false
    } else if (affected_rows == 0) {
         // 可能表示更新时值未改变
         mLogger.debug("为 UUID: {}, Currency: {} 设置余额影响了 0 行 (可能金额未改变)。", uuid, currencyType);
    } else if (affected_rows == 1) {
        // 插入了一行新记录
        mLogger.debug("为 UUID: {}, Currency: {} 插入新余额: {}", uuid, currencyType, formatBalance(amount));
    } else if (affected_rows == 2) {
        // 更新了现有记录
        mLogger.debug("为 UUID: {}, Currency: {} 更新余额为: {}", uuid, currencyType, formatBalance(amount));
    } else {
        // 其他情况 (理论上不常见)
        mLogger.warn("为 UUID: {}, Currency: {} 设置余额影响了 {} 行。", uuid, currencyType, affected_rows);
    }

    return true; // 操作成功 (即使 affected_rows 为 0 或 -1，执行本身未报错)
}


// 增加玩家余额的实现 (更新)
bool MoneyManager::addPlayerBalance(const std::string& uuid, const std::string& currencyType, int64_t amountToAdd) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法增加余额：货币类型 '{}' 未在配置中定义。", currencyType);
        return false;
    }

    // 检查增加的金额是否为正数
    if (amountToAdd <= 0) {
        mLogger.warn("尝试为 UUID: {}, Currency: {} 增加非正数金额 ({})", uuid, currencyType, formatBalance(amountToAdd));
        // 增加 0 视为成功，但无实际操作
        return amountToAdd == 0;
    }

    // --- 事务处理 (推荐) ---
    // 注意：以下事务控制代码被注释掉了，如果需要严格的原子性，应取消注释并进行测试。
    // 简单的事务控制可以通过执行 SQL 语句实现。更复杂的场景可能需要事务管理类。
    // bool transactionStarted = false;
    // if (mDbConnection.query("START TRANSACTION") == 0) {
    //     transactionStarted = true;
    // } else {
    //     mLogger.error("为增加余额启动事务失败: {}", mysql_error(mDbConnection.getMYSQL()));
    //     return false; // 无法启动事务，操作失败
    // }

    try {
        // 1. 获取当前余额，如果不存在则初始化
        int64_t currentBalance = getPlayerBalanceOrInit(uuid, currencyType);

        // 2. 检查加法是否会导致溢出
        if (currentBalance > std::numeric_limits<int64_t>::max() - amountToAdd) {
             mLogger.error("为 UUID: {}, Currency: {} 增加余额时检测到潜在溢出。当前: {}, 增加: {}",
                           uuid, currencyType, formatBalance(currentBalance), formatBalance(amountToAdd));
             // if (transactionStarted) mDbConnection.query("ROLLBACK");
             return false; // 操作失败
        }
        int64_t newBalance = currentBalance + amountToAdd; // 计算新余额

        // 3. 设置新余额 (调用 setPlayerBalance 实现更新)
        bool success = setPlayerBalance(uuid, currencyType, newBalance);

        // --- 事务结束 ---
        // if (transactionStarted) {
        //     if (success) {
        //         // 操作成功，提交事务
        //         if (mDbConnection.query("COMMIT") != 0) {
        //             mLogger.error("为增加余额提交事务失败: {}", mysql_error(mDbConnection.getMYSQL()));
        //             // 提交失败可能导致数据不一致，需要特别注意！
        //             // 此时 setPlayerBalance 可能已成功，但事务未提交。
        //             return false; // 如果提交失败，也视为操作失败
        //         }
        //     } else {
        //         // 操作失败 (setPlayerBalance 返回 false)，回滚事务
        //         if (mDbConnection.query("ROLLBACK") != 0) {
        //             // 回滚失败也需要记录错误
        //             mLogger.error("为增加余额回滚事务失败: {}", mysql_error(mDbConnection.getMYSQL()));
        //         }
        //         // success 已经是 false
        //     }
        // }

        if (success) {
             mLogger.debug("成功为 UUID: {}, Currency: {} 增加余额 {}, 新余额: {}", uuid, currencyType, formatBalance(amountToAdd), formatBalance(newBalance));
        }
        return success;

    } catch (const std::runtime_error& e) {
        // 捕获 getPlayerBalanceOrInit 可能抛出的异常 (例如初始化失败)
        mLogger.error("增加余额失败 (获取或初始化时出错): {}", e.what());
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false;
    }
}

// 减少玩家余额的实现 (保持不变，不初始化)
bool MoneyManager::subtractPlayerBalance(const std::string& uuid, const std::string& currencyType, int64_t amountToSubtract) {
    // 0. 检查货币类型是否已配置
    if (!isCurrencyConfigured(currencyType)) {
        mLogger.error("无法减少余额：货币类型 '{}' 未在配置中定义。", currencyType);
        return false;
    }

    // 检查减少的金额是否为正数
    if (amountToSubtract <= 0) {
        mLogger.warn("尝试为 UUID: {}, Currency: {} 减少非正数金额 ({})", uuid, currencyType, formatBalance(amountToSubtract));
        // 减少 0 视为成功
        return amountToSubtract == 0;
    }

    // --- 事务处理 (推荐使用) ---
    // bool transactionStarted = false;
    // if (mDbConnection.query("START TRANSACTION") == 0) {
    //     transactionStarted = true;
    // } else {
    //     mLogger.error("为减少余额启动事务失败: {}", mysql_error(mDbConnection.getMYSQL()));
    //     return false;
    // }

    // 1. 获取当前余额 (不使用 OrInit，因为扣款前必须有账户)
    std::optional<int64_t> currentBalanceOpt = getPlayerBalance(uuid, currencyType);

    // 检查账户是否存在
    if (!currentBalanceOpt.has_value()) {
        mLogger.warn("尝试从不存在的账户扣款。UUID: {}, Currency: {}", uuid, currencyType);
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false; // 账户不存在，无法扣款
    }

    int64_t currentBalance = currentBalanceOpt.value();

    // 检查余额是否足够
    if (currentBalance < amountToSubtract) {
        mLogger.warn("余额不足无法扣款。UUID: {}, Currency: {}, 当前: {}, 请求: {}",
                     uuid, currencyType, formatBalance(currentBalance), formatBalance(amountToSubtract));
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false; // 余额不足
    }

    // 检查减法是否会导致下溢 (int64_t 最小值)
    // 如果 currentBalance 接近 int64_min，减去一个正数可能导致下溢
     if (currentBalance < std::numeric_limits<int64_t>::min() + amountToSubtract) {
         mLogger.error("为 UUID: {}, Currency: {} 减少余额时检测到潜在下溢。当前: {}, 减少: {}",
                       uuid, currencyType, formatBalance(currentBalance), formatBalance(amountToSubtract));
         // if (transactionStarted) mDbConnection.query("ROLLBACK");
         return false; // 操作失败
     }

    // 2. 计算新余额
    int64_t newBalance = currentBalance - amountToSubtract;

    // 3. 设置新余额
    bool success = setPlayerBalance(uuid, currencyType, newBalance);

    // --- 事务结束 ---
    // if (transactionStarted) {
    //     if (success) {
    //         // 提交事务
    //         if (mDbConnection.query("COMMIT") != 0) {
    //             mLogger.error("为减少余额提交事务失败: {}", mysql_error(mDbConnection.getMYSQL()));
    //             return false; // 提交失败视为操作失败
    //         }
    //     } else {
    //         // 回滚事务
    //         if (mDbConnection.query("ROLLBACK") != 0) {
    //             mLogger.error("为减少余额回滚事务失败: {}", mysql_error(mDbConnection.getMYSQL()));
    //         }
    //     }
    // }

    // 记录调试信息
    if (success) {
         mLogger.debug("成功为 UUID: {}, Currency: {} 减少余额 {}, 新余额: {}", uuid, currencyType, formatBalance(amountToSubtract), formatBalance(newBalance));
    }

    return success; // 返回 setPlayerBalance 的结果
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


} // namespace czmoney
