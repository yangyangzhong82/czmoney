#include "czmoney/mysql.h"
#include <utility> // For std::move
#include <vector>  // For DbResult, DbRow
#include <variant> // For DbValue

// Define my_bool if it's not defined by mysql.h (common issue)
// my_bool is typically defined as char in older versions or specific configs.
#ifndef my_bool
#define my_bool char
#endif

namespace db {

// 构造函数实现
MySQLConnection::MySQLConnection(
    const std::string& host,
    const std::string& user,
    const std::string& password,
    const std::string& database,
    unsigned int       port
) :
    m_host(host),
    m_user(user),
    m_password(password),
    m_database(database),
    m_port(port),
    m_connection(nullptr), // 初始化指针为空
    m_connected(false) {}

// 析构函数实现
MySQLConnection::~MySQLConnection() {
    disconnect(); // 确保在对象销毁时断开连接
}

// 移动构造函数实现
MySQLConnection::MySQLConnection(MySQLConnection&& other) noexcept :
    m_host(std::move(other.m_host)),
    m_user(std::move(other.m_user)),
    m_password(std::move(other.m_password)),
    m_database(std::move(other.m_database)),
    m_port(other.m_port),
    m_connection(other.m_connection), // 转移指针所有权
    m_connected(other.m_connected) {
    // 将源对象的指针置空，防止其析构函数关闭连接
    other.m_connection = nullptr;
    other.m_connected = false;
}

// 移动赋值运算符实现
MySQLConnection& MySQLConnection::operator=(MySQLConnection&& other) noexcept {
    if (this != &other) { // 防止自赋值
        // 先释放当前对象的资源
        disconnect();

        // 移动源对象的资源
        m_host = std::move(other.m_host);
        m_user = std::move(other.m_user);
        m_password = std::move(other.m_password);
        m_database = std::move(other.m_database);
        m_port = other.m_port;
        m_connection = other.m_connection; // 转移指针所有权
        m_connected = other.m_connected;

        // 将源对象的指针置空
        other.m_connection = nullptr;
        other.m_connected = false;
    }
    return *this;
}


// 连接数据库实现
bool MySQLConnection::connect() {
    if (m_connected) {
        return true; // 已经连接，直接返回成功
    }

    // 初始化 MySQL 连接对象
    m_connection = mysql_init(nullptr);
    if (!m_connection) {
        throw MySQLException("mysql_init failed"); // 抛出异常
        return false;
    }

    // 设置字符集为 utf8mb4 
    mysql_options(m_connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    // 尝试连接数据库
    if (!mysql_real_connect(
            m_connection,
            m_host.c_str(),
            m_user.c_str(),
            m_password.c_str(),
            m_database.c_str(),
            m_port,
            nullptr, // Unix socket (usually nullptr for TCP/IP)
            0        // Client flags (0 for default)
        )) {
        // 连接失败
        // 可以记录详细错误信息: mysql_error(m_connection)
        throw MySQLException("mysql_real_connect failed", m_connection); //抛出异常
        mysql_close(m_connection); // 清理已初始化的连接对象
        m_connection = nullptr;
        return false;
    }

    // 连接成功
    m_connected = true;
    return true;
}

// 断开数据库连接实现
void MySQLConnection::disconnect() {
    if (m_connection) {
        mysql_close(m_connection);
        m_connection = nullptr;
        m_connected = false;
    }
}

// 检查连接状态实现
bool MySQLConnection::isConnected() const {
    // 可以增加 mysql_ping 来检查连接是否仍然有效，但这会产生网络开销
    // return m_connected && m_connection && (mysql_ping(m_connection) == 0);
    return m_connected && m_connection;
}

// 执行 SQL 语句实现 (符合 IDatabaseConnection 接口)
int MySQLConnection::execute(const std::string& sql) {
    if (!isConnected()) {
        throw MySQLException("Not connected to database"); // 抛出异常
        // return -1; // 或者返回错误码
    }

    if (mysql_query(m_connection, sql.c_str())) {
        // 查询执行失败
        throw MySQLException("mysql_query failed for SQL: " + sql, m_connection); // 抛出异常
        // return mysql_errno(m_connection); // 返回 MySQL 错误码
    }

    // 对于 INSERT, UPDATE, DELETE 等操作，可以返回影响的行数
    // 对于 CREATE, DROP 等 DDL 操作，通常返回 0
    // 这里简单返回 0 表示成功，与接口定义一致
    return 0; // 操作成功 (注意：这不一定是影响的行数)
}

// 获取底层 MYSQL 指针实现
MYSQL* MySQLConnection::getMYSQL() const {
    return m_connection;
}


// 实现 query 方法
DbResult MySQLConnection::query(const std::string& sql) {
    if (!isConnected()) {
        throw MySQLException("Not connected to MySQL database");
    }

    // 使用 mysql_real_query 防止 SQL 注入 (虽然这里 sql 是直接传入的)
    if (mysql_real_query(m_connection, sql.c_str(), static_cast<unsigned long>(sql.length())) != 0) {
        throw MySQLException("mysql_real_query failed", m_connection);
    }

    // 存储查询结果
    MYSQL_RES* result = mysql_store_result(m_connection);
    if (!result) {
        // 检查是查询没有返回结果集 (例如 INSERT, UPDATE) 还是发生了错误
        if (mysql_field_count(m_connection) == 0) {
            // 没有结果集 (例如 INSERT, UPDATE)，返回空 DbResult
            return {};
        } else {
            // 发生了错误
            throw MySQLException("mysql_store_result failed", m_connection);
        }
    }

    // 使用 RAII 管理 MYSQL_RES
    std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> resultGuard(result, mysql_free_result);

    DbResult dbResult; // 最终返回的结果集
    MYSQL_ROW row;
    unsigned int numFields = mysql_num_fields(result);

    // 逐行获取数据
    while ((row = mysql_fetch_row(result))) {
        DbRow dbRow;
        dbRow.reserve(numFields); // 预分配空间

        // 获取当前行的列长度 (用于处理二进制数据，虽然这里暂时不处理)
        // unsigned long* lengths = mysql_fetch_lengths(result);

        for (unsigned int i = 0; i < numFields; ++i) {
            if (row[i] == nullptr) {
                // 处理 NULL 值
                dbRow.emplace_back(nullptr);
            } else {
                // 非 NULL 值，暂时都作为字符串处理
                // TODO: 可以根据 mysql_fetch_fields 获取的类型进行更精确的转换
                dbRow.emplace_back(std::string(row[i]));
            }
        }
        dbResult.push_back(std::move(dbRow)); // 添加行到结果集
    }

    // resultGuard 会在函数结束时自动调用 mysql_free_result
    return dbResult;
}

// 实现 getDbType 方法
std::string MySQLConnection::getDbType() const {
    return "mysql";
}

// --- 事务管理实现 ---

void MySQLConnection::beginTransaction() {
    if (!isConnected()) {
        throw MySQLException("Not connected to database");
    }
    // 关闭自动提交以开始事务
    if (mysql_autocommit(m_connection, 0)) { // 0 = disable autocommit
        throw MySQLException("Failed to disable autocommit (begin transaction)", m_connection);
    }
}

void MySQLConnection::commitTransaction() {
     if (!isConnected()) {
        // 理论上不应在未连接时调用，但添加检查
        throw MySQLException("Not connected to database");
    }
    if (mysql_commit(m_connection)) {
        // 提交失败，尝试回滚并抛出异常
        mysql_rollback(m_connection); // 尝试回滚
        throw MySQLException("Failed to commit transaction", m_connection);
    }
    // 提交成功后，重新启用自动提交 (可选，取决于应用逻辑)
    // 通常在事务结束后恢复自动提交是个好习惯
    if (mysql_autocommit(m_connection, 1)) { // 1 = enable autocommit
        // 如果恢复自动提交失败，记录警告或抛出次要异常
         throw MySQLException("Failed to re-enable autocommit after commit", m_connection);
    }
}

void MySQLConnection::rollbackTransaction() {
     if (!isConnected()) {
        throw MySQLException("Not connected to database");
    }
    if (mysql_rollback(m_connection)) {
        // 回滚失败通常是严重问题
        throw MySQLException("Failed to rollback transaction", m_connection);
    }
     // 回滚后，重新启用自动提交 (可选)
    if (mysql_autocommit(m_connection, 1)) {
         throw MySQLException("Failed to re-enable autocommit after rollback", m_connection);
    }
}


// --- 预处理语句辅助 ---

// RAII 包装器，用于自动关闭 MYSQL_STMT
class MySQLStatementGuard {
    MYSQL_STMT* mStmt;
public:
    explicit MySQLStatementGuard(MYSQL_STMT* stmt = nullptr) : mStmt(stmt) {}
    ~MySQLStatementGuard() {
        if (mStmt) {
            mysql_stmt_close(mStmt);
        }
    }
    MySQLStatementGuard(const MySQLStatementGuard&) = delete;
    MySQLStatementGuard& operator=(const MySQLStatementGuard&) = delete;
    MySQLStatementGuard(MySQLStatementGuard&& other) noexcept : mStmt(other.mStmt) {
        other.mStmt = nullptr;
    }
    MySQLStatementGuard& operator=(MySQLStatementGuard&& other) noexcept {
        if (this != &other) {
            if (mStmt) mysql_stmt_close(mStmt);
            mStmt = other.mStmt;
            other.mStmt = nullptr;
        }
        return *this;
    }
    MYSQL_STMT* get() const { return mStmt; }
    void reset(MYSQL_STMT* stmt = nullptr) {
        if (mStmt) mysql_stmt_close(mStmt);
        mStmt = stmt;
    }
};

// RAII 包装器，用于管理 MYSQL_BIND 数组和相关数据
// 注意：这个实现比较基础，特别是字符串处理
class MySQLBindGuard {
    std::vector<MYSQL_BIND> mBinds;
    // 需要存储字符串数据，因为 MYSQL_BIND 中的 char* 指针需要有效
    std::vector<std::string> mStringData;
    // 可能还需要存储其他类型的数据指针，如果需要的话

public:
    MySQLBindGuard() = default;
    ~MySQLBindGuard() = default; // 内存由 vector 管理

    MySQLBindGuard(const MySQLBindGuard&) = delete;
    MySQLBindGuard& operator=(const MySQLBindGuard&) = delete;
    MySQLBindGuard(MySQLBindGuard&&) = default; // 允许移动
    MySQLBindGuard& operator=(MySQLBindGuard&&) = default;

    // 根据 DbParams 构建 MYSQL_BIND 数组
    void build(const DbParams& params) {
        mBinds.clear();
        mStringData.clear();
        mBinds.resize(params.size());
        mStringData.reserve(params.size()); // 预留空间，减少重分配

        for (size_t i = 0; i < params.size(); ++i) {
            MYSQL_BIND& bind = mBinds[i];
            const auto& param = params[i];
            std::memset(&bind, 0, sizeof(MYSQL_BIND)); // 清零结构体

            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    bind.buffer_type = MYSQL_TYPE_NULL;
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    bind.buffer_type = MYSQL_TYPE_LONGLONG;
                    bind.buffer = const_cast<int64_t*>(&arg); // 需要去掉 const
                    bind.is_unsigned = false; // 假设是有符号的
                } else if constexpr (std::is_same_v<T, double>) {
                    bind.buffer_type = MYSQL_TYPE_DOUBLE;
                    bind.buffer = const_cast<double*>(&arg);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    bind.buffer_type = MYSQL_TYPE_STRING;
                    // 存储字符串副本，确保生命周期
                    mStringData.push_back(arg);
                    bind.buffer = const_cast<char*>(mStringData.back().c_str());
                    bind.buffer_length = mStringData.back().length();
                } else {
                    throw MySQLException("Unsupported parameter type for MySQL binding");
                }
            }, param);
        }
    }

    MYSQL_BIND* get() { return mBinds.empty() ? nullptr : mBinds.data(); }
    size_t size() const { return mBinds.size(); }
};


// --- 预处理语句实现 ---

int MySQLConnection::executePrepared(const std::string& sql, const DbParams& params) {
    if (!isConnected()) {
        throw MySQLException("Not connected to MySQL database");
    }

    // 1. 初始化语句句柄
    MYSQL_STMT* stmt = mysql_stmt_init(m_connection);
    if (!stmt) {
        throw MySQLException("mysql_stmt_init failed", m_connection);
    }
    MySQLStatementGuard stmtGuard(stmt); // RAII

    // 2. 准备语句
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
        throw MySQLException("mysql_stmt_prepare failed for SQL: " + sql, stmt);
    }

    // 检查参数数量 (可选但推荐)
    unsigned long expectedParams = mysql_stmt_param_count(stmt);
    if (expectedParams != params.size()) {
         throw MySQLException("Parameter count mismatch: SQL expects " + std::to_string(expectedParams) +
                              ", but " + std::to_string(params.size()) + " provided.", stmt);
    }

    // 3. 绑定参数
    MySQLBindGuard paramBinds;
    if (!params.empty()) {
        paramBinds.build(params);
        if (mysql_stmt_bind_param(stmt, paramBinds.get())) {
            throw MySQLException("mysql_stmt_bind_param failed", stmt);
        }
    }

    // 4. 执行语句
    if (mysql_stmt_execute(stmt)) {
        throw MySQLException("mysql_stmt_execute failed", stmt);
    }

    // 5. 获取受影响的行数
    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);

    // stmtGuard 会自动调用 mysql_stmt_close
    // 注意：返回类型是 int，可能无法完全表示 my_ulonglong
    if (affected_rows > static_cast<my_ulonglong>(std::numeric_limits<int>::max())) {
         // 可以记录警告或抛出异常，表示结果可能被截断
         // 这里暂时截断
    }
    return static_cast<int>(affected_rows);
}


DbResult MySQLConnection::queryPrepared(const std::string& sql, const DbParams& params) {
     if (!isConnected()) {
        throw MySQLException("Not connected to MySQL database");
    }

    // 1. 初始化语句句柄
    MYSQL_STMT* stmt = mysql_stmt_init(m_connection);
    if (!stmt) {
        throw MySQLException("mysql_stmt_init failed", m_connection);
    }
    MySQLStatementGuard stmtGuard(stmt);

    // 2. 准备语句
    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length()))) {
        throw MySQLException("mysql_stmt_prepare failed for SQL: " + sql, stmt);
    }

    // 检查参数数量
     unsigned long expectedParams = mysql_stmt_param_count(stmt);
    if (expectedParams != params.size()) {
         throw MySQLException("Parameter count mismatch: SQL expects " + std::to_string(expectedParams) +
                              ", but " + std::to_string(params.size()) + " provided.", stmt);
    }

    // 3. 绑定参数
    MySQLBindGuard paramBinds;
     if (!params.empty()) {
        paramBinds.build(params);
        if (mysql_stmt_bind_param(stmt, paramBinds.get())) {
            throw MySQLException("mysql_stmt_bind_param failed", stmt);
        }
    }

    // 4. 执行语句
    if (mysql_stmt_execute(stmt)) {
        throw MySQLException("mysql_stmt_execute failed", stmt);
    }

    // 5. 获取结果元数据 (字段信息)
    MYSQL_RES* meta_result = mysql_stmt_result_metadata(stmt);
    if (!meta_result) {
        // 可能是没有结果集的语句 (如 UPDATE)，或者错误
        if (mysql_stmt_field_count(stmt) == 0) {
            return {}; // 没有结果集，返回空
        } else {
            throw MySQLException("mysql_stmt_result_metadata failed", stmt);
        }
    }
    // 使用 unique_ptr 管理元数据结果集
    std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> metaGuard(meta_result, mysql_free_result);

    unsigned int numFields = mysql_num_fields(meta_result);

    // 6. 准备结果绑定
    std::vector<MYSQL_BIND> resultBinds(numFields);
    std::vector<std::vector<char>> resultBuffers(numFields); // 存储实际数据
    std::vector<unsigned long> lengths(numFields);          // 存储数据长度
    std::vector<char> is_null(numFields);                   // 存储是否为 NULL (使用 char)
    std::vector<char> error(numFields);                     // 存储列错误状态 (使用 char)

    for (unsigned int i = 0; i < numFields; ++i) {
        MYSQL_FIELD* field = mysql_fetch_field_direct(meta_result, i);
        MYSQL_BIND& bind = resultBinds[i];
        std::memset(&bind, 0, sizeof(MYSQL_BIND));

        // 根据字段类型预分配缓冲区大小 (这是一个简化，可能需要调整)
        // 对于字符串和 BLOB，大小可能需要动态调整或使用 mysql_stmt_fetch 获取长度后再分配
        size_t bufferSize = 32; // 默认大小
        if (field->type == MYSQL_TYPE_VAR_STRING || field->type == MYSQL_TYPE_STRING || field->type == MYSQL_TYPE_BLOB || field->type == MYSQL_TYPE_LONG_BLOB) {
            bufferSize = field->max_length > 0 ? field->max_length + 1 : 1024; // 使用 max_length 或默认值
        } else if (field->type == MYSQL_TYPE_LONGLONG) {
             bufferSize = sizeof(int64_t);
        } else if (field->type == MYSQL_TYPE_DOUBLE) {
             bufferSize = sizeof(double);
        }
        // ... 其他类型

        resultBuffers[i].resize(bufferSize);

        bind.buffer_type = field->type;
        bind.buffer = resultBuffers[i].data();
        bind.buffer_length = bufferSize;
        bind.length = &lengths[i];
        // Linter seems to expect bool*, cast char* to bool*
        bind.is_null = reinterpret_cast<bool*>(&is_null[i]);
        bind.error = reinterpret_cast<bool*>(&error[i]);
    }

    // 7. 绑定结果缓冲区
    if (mysql_stmt_bind_result(stmt, resultBinds.data())) {
        throw MySQLException("mysql_stmt_bind_result failed", stmt);
    }

    // 8. 存储结果集 (将所有结果拉到客户端)
    if (mysql_stmt_store_result(stmt)) {
        throw MySQLException("mysql_stmt_store_result failed", stmt);
    }

    // 9. 逐行获取数据
    DbResult finalResult;
    while (true) {
        int fetch_rc = mysql_stmt_fetch(stmt);
        if (fetch_rc == MYSQL_NO_DATA) {
            break; // 没有更多行了
        }
        if (fetch_rc == 1) { // 1 表示错误
            throw MySQLException("mysql_stmt_fetch failed", stmt);
        }
        // fetch_rc == MYSQL_DATA_TRUNCATED (警告，数据可能被截断)
        // 这里可以选择记录警告或抛出异常

        DbRow currentRow;
        currentRow.reserve(numFields);

        for (unsigned int i = 0; i < numFields; ++i) {
            // 使用 char vector 的值 (0 或非 0)
            if (is_null[i]) {
                currentRow.emplace_back(nullptr);
            } else {
                // 根据 buffer_type 将缓冲区数据转换为 DbValue
                // 注意：这是一个简化版本，需要更完善的类型处理
                switch (resultBinds[i].buffer_type) {
                    case MYSQL_TYPE_LONGLONG:
                        if (lengths[i] == sizeof(int64_t)) {
                             currentRow.emplace_back(*reinterpret_cast<int64_t*>(resultBuffers[i].data()));
                        } else {
                             // 类型不匹配或长度错误
                             currentRow.emplace_back("[Invalid INT64]");
                        }
                        break;
                    case MYSQL_TYPE_DOUBLE:
                         if (lengths[i] == sizeof(double)) {
                             currentRow.emplace_back(*reinterpret_cast<double*>(resultBuffers[i].data()));
                         } else {
                             currentRow.emplace_back("[Invalid DOUBLE]");
                         }
                        break;
                    case MYSQL_TYPE_VAR_STRING:
                    case MYSQL_TYPE_STRING:
                    case MYSQL_TYPE_BLOB: // 将 BLOB 也视为字符串
                    case MYSQL_TYPE_LONG_BLOB:
                        // 使用实际长度 lengths[i] 创建字符串
                        currentRow.emplace_back(std::string(resultBuffers[i].data(), lengths[i]));
                        break;
                    case MYSQL_TYPE_NULL: // 理论上已被 is_null 处理
                         currentRow.emplace_back(nullptr);
                         break;
                    // 添加其他需要的类型转换...
                    default:
                        currentRow.emplace_back("[Unsupported Type]");
                        break;
                }
                 // 检查是否有列错误 (使用 char vector 的值)
                 if (error[i]) {
                     // 可以记录警告或修改值
                     // currentRow.back() = "[Error]";
                 }
                 // 检查是否截断 (fetch_rc == MYSQL_DATA_TRUNCATED)
                 // if (fetch_rc == MYSQL_DATA_TRUNCATED && lengths[i] > resultBuffers[i].size()) {
                 //     // 数据被截断，可能需要重新 fetch 或处理
                 // }
            }
        }
        finalResult.push_back(std::move(currentRow));
    }

    // metaGuard 和 stmtGuard 会自动释放资源
    return finalResult;
}


} // namespace db
