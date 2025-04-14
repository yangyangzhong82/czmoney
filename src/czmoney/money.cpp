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

    // 初始化流水日志表
    if (!initializeLogTable()) {
        mLogger.error("初始化 'economy_log' 表失败。");
        return false; // 如果日志表初始化失败，整个初始化也失败
    }

    return true; // 初始化成功
}

// 新增：初始化经济流水日志表的实现
bool MoneyManager::initializeLogTable() {
    if (!mDbConnection.isConnected()) {
        mLogger.error("无法初始化流水表：数据库未连接。");
        return false;
    }

    const char* createLogTableSQL = R"(
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

    if (mDbConnection.query(createLogTableSQL) != 0) {
        mLogger.error("创建或验证 'economy_log' 表失败: {}", mysql_error(mDbConnection.getMYSQL()));
        return false;
    }

    mLogger.info("'economy_log' 表初始化成功。");
    return true;
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

// 新增：记录经济交易流水的实现
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

    MYSQL* mysql = mDbConnection.getMYSQL();
    MYSQL_STMT* stmt = mysql_stmt_init(mysql);
    if (!stmt) {
        mLogger.error("mysql_stmt_init() 失败 (记录流水): {}", mysql_error(mysql));
        return false;
    }
    StatementGuard stmtGuard(stmt);

    const std::string sql = R"(
        INSERT INTO economy_log (uuid, currency_type, change_amount, previous_amount, reason1, reason2, reason3)
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )";

    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
        mLogger.error("mysql_stmt_prepare() 失败 (记录流水): {}", mysql_stmt_error(stmt));
        return false;
    }

    // --- 绑定输入参数 ---
    MYSQL_BIND params[7]; // 7 个参数
    memset(params, 0, sizeof(params));

    unsigned long uuidLen = static_cast<unsigned long>(uuid.length());
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = const_cast<char*>(uuid.c_str());
    params[0].buffer_length = uuidLen;
    params[0].length = &uuidLen;

    unsigned long currencyTypeLen = static_cast<unsigned long>(currencyType.length());
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = const_cast<char*>(currencyType.c_str());
    params[1].buffer_length = currencyTypeLen;
    params[1].length = &currencyTypeLen;

    params[2].buffer_type = MYSQL_TYPE_LONGLONG;
    params[2].buffer = &changeAmount;
    params[2].is_unsigned = false;

    params[3].buffer_type = MYSQL_TYPE_LONGLONG;
    params[3].buffer = &previousAmount;
    params[3].is_unsigned = false;

    // 绑定理由，处理空字符串的情况 (插入 NULL)
    unsigned long reason1Len = static_cast<unsigned long>(reason1.length());
    bool reason1_is_null = reason1.empty();
    params[4].buffer_type = MYSQL_TYPE_STRING;
    params[4].buffer = const_cast<char*>(reason1.c_str());
    params[4].buffer_length = reason1Len;
    params[4].length = &reason1Len;
    params[4].is_null = &reason1_is_null;

    unsigned long reason2Len = static_cast<unsigned long>(reason2.length());
    bool reason2_is_null = reason2.empty();
    params[5].buffer_type = MYSQL_TYPE_STRING;
    params[5].buffer = const_cast<char*>(reason2.c_str());
    params[5].buffer_length = reason2Len;
    params[5].length = &reason2Len;
    params[5].is_null = &reason2_is_null;

    unsigned long reason3Len = static_cast<unsigned long>(reason3.length());
    bool reason3_is_null = reason3.empty();
    params[6].buffer_type = MYSQL_TYPE_STRING;
    params[6].buffer = const_cast<char*>(reason3.c_str());
    params[6].buffer_length = reason3Len;
    params[6].length = &reason3Len;
    params[6].is_null = &reason3_is_null;


    if (mysql_stmt_bind_param(stmt, params)) {
        mLogger.error("mysql_stmt_bind_param() 失败 (记录流水): {}", mysql_stmt_error(stmt));
        return false;
    }
    // --- 输入参数绑定结束 ---

    if (mysql_stmt_execute(stmt)) {
        mLogger.error("mysql_stmt_execute() 失败 (记录流水): {}", mysql_stmt_error(stmt));
        return false;
    }

    // 检查受影响的行数，确保插入成功
    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
    if (affected_rows != 1) {
        mLogger.warn("记录流水影响了 {} 行 (预期为 1)。UUID: {}, Currency: {}", affected_rows, uuid, currencyType);
        // 根据策略，这里可以返回 false，或者仅记录警告
        // return false;
    }

    mLogger.debug("成功记录流水: UUID={}, Type={}, Change={}, Prev={}, R1={}, R2={}, R3={}",
                  uuid, currencyType, formatBalance(changeAmount), formatBalance(previousAmount),
                  reason1.empty() ? "NULL" : reason1,
                  reason2.empty() ? "NULL" : reason2,
                  reason3.empty() ? "NULL" : reason3);

    return true;
}


// 设置玩家余额的实现 (更新)
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
        // 这主要用于记录 set 操作创建新账户时的流水
        auto it = mConfig.economy.find(currencyType);
        if (it != mConfig.economy.end()) {
            // 使用辅助函数安全转换配置中的 double 初始值
            std::optional<int64_t> initialBalanceIntOpt = convertDoubleToInt64(
                it->second.initialBalance,
                "initialBalance for " + currencyType + " (in setPlayerBalance fallback)"
            );
            if (initialBalanceIntOpt.has_value()) {
                previousBalance = initialBalanceIntOpt.value();
            } else {
                // 如果转换失败（例如配置值无效），保持 previousBalance 为 0 并记录错误
                mLogger.error(
                    "无法转换配置中货币类型 '{}' 的 initialBalance ({}) 作为 setPlayerBalance 的 previousBalance 回退值。",
                    currencyType,
                    it->second.initialBalance
                );
                // previousBalance 保持 0
            }
        }
        // 如果连配置都没有，previousBalance 保持 0
    }
    // --- 获取操作前余额结束 ---

    // --- 执行数据库更新 ---
    MYSQL* mysql = mDbConnection.getMYSQL();
    MYSQL_STMT* stmt = mysql_stmt_init(mysql);
    if (!stmt) {
        mLogger.error("mysql_stmt_init() 失败 (设置余额): {}", mysql_error(mysql));
        return false;
    }
    StatementGuard stmtGuard(stmt);

    const std::string sql = R"(
        INSERT INTO player_balances (uuid, currency_type, amount)
        VALUES (?, ?, ?)
        ON DUPLICATE KEY UPDATE amount = VALUES(amount);
    )";

    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
        mLogger.error("mysql_stmt_prepare() 失败 (设置余额): {}", mysql_stmt_error(stmt));
        return false;
    }

    MYSQL_BIND params[3];
    memset(params, 0, sizeof(params));

    unsigned long uuidLen = static_cast<unsigned long>(uuid.length());
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = const_cast<char*>(uuid.c_str());
    params[0].buffer_length = uuidLen;
    params[0].length = &uuidLen;

    unsigned long currencyTypeLen = static_cast<unsigned long>(currencyType.length());
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = const_cast<char*>(currencyType.c_str());
    params[1].buffer_length = currencyTypeLen;
    params[1].length = &currencyTypeLen;

    params[2].buffer_type = MYSQL_TYPE_LONGLONG;
    params[2].buffer = &amount;
    params[2].is_unsigned = false;

    if (mysql_stmt_bind_param(stmt, params)) {
        mLogger.error("mysql_stmt_bind_param() 失败 (设置余额): {}", mysql_stmt_error(stmt));
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        mLogger.error("mysql_stmt_execute() 失败 (设置余额): {}", mysql_stmt_error(stmt));
        return false;
    }
    // --- 数据库更新结束 ---

    // --- 检查并记录流水 ---
    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
    bool dbUpdateSucceeded = (affected_rows != (my_ulonglong)-1); // 只要执行没报错就算成功

    if (dbUpdateSucceeded) {
        int64_t changeAmount = amount - previousBalance; // 计算实际变动值
        // 只有当金额实际发生变化时才记录日志 (affected_rows > 0 或插入新行时 affected_rows == 1)
        // 或者即使金额未变 (affected_rows == 0)，也记录 set 操作？(当前选择：仅在值变化时记录)
        if (changeAmount != 0) {
             if (!logTransaction(uuid, currencyType, changeAmount, previousBalance, reason1, reason2, reason3)) {
                 // 记录流水失败，这是一个严重问题，可能导致数据不一致
                 mLogger.error("数据库余额已更新，但记录流水失败！UUID: {}, Currency: {}", uuid, currencyType);
                 // 根据策略，这里可以尝试回滚，或者标记错误，或者返回 false
                 // return false; // 如果要求流水必须成功
             }
        } else {
             mLogger.debug("Set 操作未改变余额，不记录流水。UUID: {}, Currency: {}", uuid, currencyType);
        }

        // 记录调试信息 (基于 affected_rows)
        if (affected_rows == 0) {
            mLogger.debug("为 UUID: {}, Currency: {} 设置余额影响了 0 行 (金额未改变)。", uuid, currencyType);
        } else if (affected_rows == 1) {
            mLogger.debug("为 UUID: {}, Currency: {} 插入新余额: {}", uuid, currencyType, formatBalance(amount));
        } else if (affected_rows == 2) {
            mLogger.debug("为 UUID: {}, Currency: {} 更新余额为: {}", uuid, currencyType, formatBalance(amount));
        } else if (affected_rows == (my_ulonglong)-1) {
             mLogger.error("获取受影响行数时出错 (设置余额) UUID: {}, Currency: {}", uuid, currencyType);
        } else {
            mLogger.warn("为 UUID: {}, Currency: {} 设置余额影响了 {} 行。", uuid, currencyType, affected_rows);
        }
    } else {
         // 数据库更新本身失败了，不需要记录流水
         mLogger.error("设置余额的数据库操作失败，不记录流水。UUID: {}, Currency: {}", uuid, currencyType);
         return false; // 数据库操作失败，返回 false
    }

    return true; // 数据库操作成功（即使流水记录可能失败，根据上面的策略）
}


// 增加玩家余额的实现 (更新)
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

    // --- 事务处理 (可选，但推荐) ---
    // bool transactionStarted = mDbConnection.query("START TRANSACTION") == 0;
    // if (!transactionStarted) mLogger.error("启动事务失败 (增加余额)");

    try {
        // 1. 获取当前余额，如果不存在则初始化
        int64_t currentBalance = getPlayerBalanceOrInit(uuid, currencyType);

        // 2. 检查溢出
        if (currentBalance > std::numeric_limits<int64_t>::max() - amountToAdd) {
             mLogger.error("增加余额时检测到潜在溢出。UUID: {}, Currency: {}", uuid, currencyType);
             // if (transactionStarted) mDbConnection.query("ROLLBACK");
             return false;
        }
        int64_t newBalance = currentBalance + amountToAdd;

        // 3. 设置新余额 (不再调用 setPlayerBalance 避免重复记录流水)
        //    直接执行数据库更新操作
        bool dbUpdateSuccess = false;
        { // 创建一个作用域以确保 stmtGuard 正确释放
            MYSQL* mysql = mDbConnection.getMYSQL();
            MYSQL_STMT* stmt = mysql_stmt_init(mysql);
            if (!stmt) {
                mLogger.error("mysql_stmt_init() 失败 (增加余额更新): {}", mysql_error(mysql));
                // if (transactionStarted) mDbConnection.query("ROLLBACK");
                return false;
            }
            StatementGuard stmtGuard(stmt);

            const std::string sql = R"(
                INSERT INTO player_balances (uuid, currency_type, amount)
                VALUES (?, ?, ?)
                ON DUPLICATE KEY UPDATE amount = VALUES(amount);
            )";

            if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
                mLogger.error("mysql_stmt_prepare() 失败 (增加余额更新): {}", mysql_stmt_error(stmt));
                // if (transactionStarted) mDbConnection.query("ROLLBACK");
                return false;
            }

            MYSQL_BIND params[3];
            memset(params, 0, sizeof(params));
            unsigned long uuidLen = static_cast<unsigned long>(uuid.length());
            params[0].buffer_type = MYSQL_TYPE_STRING; params[0].buffer = const_cast<char*>(uuid.c_str()); params[0].buffer_length = uuidLen; params[0].length = &uuidLen;
            unsigned long currencyTypeLen = static_cast<unsigned long>(currencyType.length());
            params[1].buffer_type = MYSQL_TYPE_STRING; params[1].buffer = const_cast<char*>(currencyType.c_str()); params[1].buffer_length = currencyTypeLen; params[1].length = &currencyTypeLen;
            params[2].buffer_type = MYSQL_TYPE_LONGLONG; params[2].buffer = &newBalance; params[2].is_unsigned = false;

            if (mysql_stmt_bind_param(stmt, params)) {
                mLogger.error("mysql_stmt_bind_param() 失败 (增加余额更新): {}", mysql_stmt_error(stmt));
                // if (transactionStarted) mDbConnection.query("ROLLBACK");
                return false;
            }

            if (mysql_stmt_execute(stmt)) {
                mLogger.error("mysql_stmt_execute() 失败 (增加余额更新): {}", mysql_stmt_error(stmt));
                // if (transactionStarted) mDbConnection.query("ROLLBACK");
                return false;
            }
            dbUpdateSuccess = (mysql_stmt_affected_rows(stmt) != (my_ulonglong)-1);
        } // stmtGuard 在此作用域结束时释放 stmt

        // 4. 如果数据库更新成功，记录流水
        if (dbUpdateSuccess) {
            if (!logTransaction(uuid, currencyType, amountToAdd, currentBalance, reason1, reason2, reason3)) {
                mLogger.error("数据库余额已更新，但记录流水失败！(增加余额) UUID: {}, Currency: {}", uuid, currencyType);
                // 根据策略决定是否回滚或返回失败
                // if (transactionStarted) mDbConnection.query("ROLLBACK");
                // return false;
            }
            mLogger.debug("成功为 UUID: {}, Currency: {} 增加余额 {}, 新余额: {}", uuid, currencyType, formatBalance(amountToAdd), formatBalance(newBalance));
            // if (transactionStarted && mDbConnection.query("COMMIT") != 0) {
            //     mLogger.error("提交事务失败 (增加余额)");
            //     return false; // 提交失败也算失败
            // }
            return true; // 数据库更新和流水记录（或尝试记录）都完成
        } else {
            // 数据库更新失败
            // if (transactionStarted) mDbConnection.query("ROLLBACK");
            return false;
        }

    } catch (const std::runtime_error& e) {
        mLogger.error("增加余额失败 (获取或初始化时出错): {}", e.what());
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false;
    }
}

// 减少玩家余额的实现 (更新)
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

    // --- 事务处理 (可选，但推荐) ---
    // bool transactionStarted = mDbConnection.query("START TRANSACTION") == 0;
    // if (!transactionStarted) mLogger.error("启动事务失败 (减少余额)");

    // 1. 获取当前余额 (不初始化)
    std::optional<int64_t> currentBalanceOpt = getPlayerBalance(uuid, currencyType);

    if (!currentBalanceOpt.has_value()) {
        mLogger.warn("尝试从不存在的账户扣款。UUID: {}, Currency: {}", uuid, currencyType);
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false;
    }
    int64_t currentBalance = currentBalanceOpt.value();

    // 2. 检查余额是否足够 (原始检查)
    if (currentBalance < amountToSubtract) {
        mLogger.warn("余额不足无法扣款。UUID: {}, Currency: {}, 当前: {}, 请求: {}",
                     uuid, currencyType, formatBalance(currentBalance), formatBalance(amountToSubtract));
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false;
    }

    // 3. 检查下溢
     if (currentBalance < std::numeric_limits<int64_t>::min() + amountToSubtract) {
         mLogger.error("减少余额时检测到潜在下溢。UUID: {}, Currency: {}", uuid, currencyType);
         // if (transactionStarted) mDbConnection.query("ROLLBACK");
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
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
        return false;
    }


    // 4. 设置新余额 (直接执行数据库更新)
    bool dbUpdateSuccess = false;
    {
        MYSQL* mysql = mDbConnection.getMYSQL();
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        if (!stmt) {
            mLogger.error("mysql_stmt_init() 失败 (减少余额更新): {}", mysql_error(mysql));
            // if (transactionStarted) mDbConnection.query("ROLLBACK");
            return false;
        }
        StatementGuard stmtGuard(stmt);

        // 这里使用 UPDATE 而不是 INSERT...ON DUPLICATE KEY UPDATE，因为我们已经确认账户存在
        const std::string sql = "UPDATE player_balances SET amount = ? WHERE uuid = ? AND currency_type = ?";

        if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
            mLogger.error("mysql_stmt_prepare() 失败 (减少余额更新): {}", mysql_stmt_error(stmt));
            // if (transactionStarted) mDbConnection.query("ROLLBACK");
            return false;
        }

        MYSQL_BIND params[3];
        memset(params, 0, sizeof(params));
        params[0].buffer_type = MYSQL_TYPE_LONGLONG; params[0].buffer = &newBalance; params[0].is_unsigned = false;
        unsigned long uuidLen = static_cast<unsigned long>(uuid.length());
        params[1].buffer_type = MYSQL_TYPE_STRING; params[1].buffer = const_cast<char*>(uuid.c_str()); params[1].buffer_length = uuidLen; params[1].length = &uuidLen;
        unsigned long currencyTypeLen = static_cast<unsigned long>(currencyType.length());
        params[2].buffer_type = MYSQL_TYPE_STRING; params[2].buffer = const_cast<char*>(currencyType.c_str()); params[2].buffer_length = currencyTypeLen; params[2].length = &currencyTypeLen;


        if (mysql_stmt_bind_param(stmt, params)) {
            mLogger.error("mysql_stmt_bind_param() 失败 (减少余额更新): {}", mysql_stmt_error(stmt));
            // if (transactionStarted) mDbConnection.query("ROLLBACK");
            return false;
        }

        if (mysql_stmt_execute(stmt)) {
            mLogger.error("mysql_stmt_execute() 失败 (减少余额更新): {}", mysql_stmt_error(stmt));
            // if (transactionStarted) mDbConnection.query("ROLLBACK");
            return false;
        }
        // 检查 UPDATE 是否成功影响了 1 行
        my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
        if (affected_rows == 1) {
            dbUpdateSuccess = true;
        } else if (affected_rows == 0) {
            // 可能并发修改导致条件不满足，或者值未变？
            mLogger.warn("减少余额的 UPDATE 操作影响了 0 行。UUID: {}, Currency: {}", uuid, currencyType);
            // 视为失败，因为我们期望更新一行
            dbUpdateSuccess = false;
        } else {
            mLogger.error("减少余额的 UPDATE 操作影响了 {} 行 (预期 1)。UUID: {}, Currency: {}", affected_rows, uuid, currencyType);
            dbUpdateSuccess = false; // 异常情况，视为失败
        }
    } // stmtGuard 释放 stmt

    // 5. 如果数据库更新成功，记录流水
    if (dbUpdateSuccess) {
        // 注意：changeAmount 是负数
        if (!logTransaction(uuid, currencyType, -amountToSubtract, currentBalance, reason1, reason2, reason3)) {
            mLogger.error("数据库余额已更新，但记录流水失败！(减少余额) UUID: {}, Currency: {}", uuid, currencyType);
            // 根据策略决定是否回滚或返回失败
            // if (transactionStarted) mDbConnection.query("ROLLBACK");
            // return false;
        }
         mLogger.debug("成功为 UUID: {}, Currency: {} 减少余额 {}, 新余额: {}", uuid, currencyType, formatBalance(amountToSubtract), formatBalance(newBalance));
        // if (transactionStarted && mDbConnection.query("COMMIT") != 0) {
        //     mLogger.error("提交事务失败 (减少余额)");
        //     return false;
        // }
        return true;
    } else {
        // 数据库更新失败
        // if (transactionStarted) mDbConnection.query("ROLLBACK");
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


} // namespace czmoney
