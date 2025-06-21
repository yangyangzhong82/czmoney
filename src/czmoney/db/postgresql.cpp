#include "czmoney/db/postgresql.h"
#include <algorithm> // For std::transform
#include <sstream>   // For std::stringstream
#include <utility>   // For std::move
#include <variant>   // For DbValue
#include <vector>    // For DbResult, DbRow


namespace db {

// 构造函数实现
PostgreSQLConnection::PostgreSQLConnection(
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
PostgreSQLConnection::~PostgreSQLConnection() {
    disconnect(); // 确保在对象销毁时断开连接
}

// 移动构造函数实现
PostgreSQLConnection::PostgreSQLConnection(PostgreSQLConnection&& other) noexcept :
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
PostgreSQLConnection& PostgreSQLConnection::operator=(PostgreSQLConnection&& other) noexcept {
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
bool PostgreSQLConnection::connect() {
    if (m_connected) {
        return true; // 已经连接，直接返回成功
    }

    // 构建连接字符串
    std::stringstream connInfo;
    connInfo << "host=" << m_host
             << " port=" << m_port
             << " dbname=" << m_database
             << " user=" << m_user
             << " password=" << m_password
             << " client_encoding=UTF8"; // 设置客户端编码为 UTF-8

    // 尝试连接数据库
    m_connection = PQconnectdb(connInfo.str().c_str());
    
    if (!m_connection) {
        throw PostgreSQLException("PQconnectdb failed: unable to allocate connection object");
        return false;
    }

    // 检查连接状态
    if (PQstatus(m_connection) != CONNECTION_OK) {
        // 连接失败
        throw PostgreSQLException("PostgreSQL connection failed", m_connection);
        PQfinish(m_connection); // 清理连接对象
        m_connection = nullptr;
        return false;
    }

    // 连接成功
    m_connected = true;
    return true;
}

// 断开数据库连接实现
void PostgreSQLConnection::disconnect() {
    if (m_connection) {
        PQfinish(m_connection);
        m_connection = nullptr;
        m_connected = false;
    }
}

// 检查连接状态实现
bool PostgreSQLConnection::isConnected() const {
    // 检查连接是否有效
    return m_connected && m_connection && (PQstatus(m_connection) == CONNECTION_OK);
}

// 辅助函数：检查查询结果状态
void PostgreSQLConnection::checkResult(PGresult* result, ExecStatusType expectedStatus, const std::string& errorMessage) {
    if (!result) {
        throw PostgreSQLException(errorMessage + ": result is null", m_connection);
    }
    
    ExecStatusType status = PQresultStatus(result);
    if (status != expectedStatus) {
        throw PostgreSQLException(errorMessage, result);
    }
}

// 执行 SQL 语句实现 (符合 IDatabaseConnection 接口)
int PostgreSQLConnection::execute(const std::string& sql) {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to PostgreSQL database");
    }

    PGresult* result = PQexec(m_connection, sql.c_str());
    if (!result) {
        throw PostgreSQLException("PQexec failed for SQL: " + sql, m_connection);
    }

    // 使用 RAII 管理 PGresult
    std::unique_ptr<PGresult, decltype(&PQclear)> resultGuard(result, PQclear);

    ExecStatusType status = PQresultStatus(result);
    
    // 检查执行状态
    switch (status) {
        case PGRES_COMMAND_OK:  // 成功执行的命令 (INSERT, UPDATE, DELETE, CREATE, etc.)
        case PGRES_TUPLES_OK:   // 成功执行的查询 (SELECT)
            break;
        default:
            throw PostgreSQLException("PostgreSQL command failed for SQL: " + sql, result);
    }

    // 对于 INSERT, UPDATE, DELETE 等操作，可以返回影响的行数
    // 对于 CREATE, DROP 等 DDL 操作，通常返回 0
    return 0; // 操作成功
}

// 实现 query 方法
DbResult PostgreSQLConnection::query(const std::string& sql) {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to PostgreSQL database");
    }

    PGresult* result = PQexec(m_connection, sql.c_str());
    if (!result) {
        throw PostgreSQLException("PQexec failed", m_connection);
    }

    // 使用 RAII 管理 PGresult
    std::unique_ptr<PGresult, decltype(&PQclear)> resultGuard(result, PQclear);

    ExecStatusType status = PQresultStatus(result);
    
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        throw PostgreSQLException("PostgreSQL query failed", result);
    }

    // 如果是没有结果集的命令 (例如 INSERT, UPDATE)，返回空结果
    if (status == PGRES_COMMAND_OK) {
        return {};
    }

    DbResult dbResult; // 最终返回的结果集
    int numRows = PQntuples(result);
    int numFields = PQnfields(result);

    // 逐行获取数据
    for (int row = 0; row < numRows; ++row) {
        DbRow dbRow;
        dbRow.reserve(numFields); // 预分配空间

        for (int col = 0; col < numFields; ++col) {
            if (PQgetisnull(result, row, col)) {
                // 处理 NULL 值
                dbRow.emplace_back(nullptr);
            } else {
                // 非 NULL 值，获取字段值
                char* value = PQgetvalue(result, row, col);
                // TODO: 可以根据 PQftype 获取的类型进行更精确的转换
                // 暂时都作为字符串处理
                dbRow.emplace_back(std::string(value));
            }
        }
        dbResult.push_back(std::move(dbRow)); // 添加行到结果集
    }

    return dbResult;
}

// 实现 getDbType 方法
std::string PostgreSQLConnection::getDbType() const {
    return "postgresql";
}

// 获取底层 PGconn 指针实现
PGconn* PostgreSQLConnection::getPGconn() const {
    return m_connection;
}

// --- 事务管理实现 ---

void PostgreSQLConnection::beginTransaction() {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to database");
    }
    
    PGresult* result = PQexec(m_connection, "BEGIN");
    checkResult(result, PGRES_COMMAND_OK, "Failed to begin transaction");
    PQclear(result);
}

void PostgreSQLConnection::commitTransaction() {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to database");
    }
    
    PGresult* result = PQexec(m_connection, "COMMIT");
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        PQclear(result);
        // 提交失败，尝试回滚
        PGresult* rollbackResult = PQexec(m_connection, "ROLLBACK");
        PQclear(rollbackResult); // 清理回滚结果
        throw PostgreSQLException("Failed to commit transaction", result);
    }
    PQclear(result);
}

void PostgreSQLConnection::rollbackTransaction() {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to database");
    }
    
    PGresult* result = PQexec(m_connection, "ROLLBACK");
    checkResult(result, PGRES_COMMAND_OK, "Failed to rollback transaction");
    PQclear(result);
}

// 辅助函数：将 DbValue 转换为字符串
std::string PostgreSQLConnection::valueToString(const DbValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return ""; // NULL 值在参数数组中用 nullptr 表示
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else {
            throw PostgreSQLException("Unsupported parameter type for PostgreSQL binding");
        }
    }, value);
}

// --- 预处理语句实现 ---

int PostgreSQLConnection::executePrepared(const std::string& sql, const DbParams& params) {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to PostgreSQL database");
    }

    // 准备参数
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    std::vector<std::string> stringParams; // 存储字符串参数，确保生命周期

    paramValues.reserve(params.size());
    paramLengths.reserve(params.size());
    paramFormats.reserve(params.size());
    stringParams.reserve(params.size());

    for (const auto& param : params) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                paramValues.push_back(nullptr);
                paramLengths.push_back(0);
                paramFormats.push_back(0); // 文本格式
            } else {
                // 转换为字符串
                stringParams.push_back(valueToString(param));
                paramValues.push_back(stringParams.back().c_str());
                paramLengths.push_back(static_cast<int>(stringParams.back().length()));
                paramFormats.push_back(0); // 文本格式
            }
        }, param);
    }

    // 执行预处理语句
    PGresult* result = PQexecParams(
        m_connection,
        sql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes (让 PostgreSQL 自动推断)
        paramValues.data(),
        paramLengths.data(),
        paramFormats.data(),
        0 // 结果格式 (0 = 文本格式)
    );

    if (!result) {
        throw PostgreSQLException("PQexecParams failed", m_connection);
    }

    // 使用 RAII 管理 PGresult
    std::unique_ptr<PGresult, decltype(&PQclear)> resultGuard(result, PQclear);

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        throw PostgreSQLException("PostgreSQL prepared statement execution failed", result);
    }

    // 获取受影响的行数
    char* affected = PQcmdTuples(result);
    if (affected && strlen(affected) > 0) {
        return std::atoi(affected);
    }

    return 0; // 操作成功，但没有受影响的行数信息
}

DbResult PostgreSQLConnection::queryPrepared(const std::string& sql, const DbParams& params) {
    if (!isConnected()) {
        throw PostgreSQLException("Not connected to PostgreSQL database");
    }

    // 准备参数 (与 executePrepared 相同的逻辑)
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    std::vector<std::string> stringParams;

    paramValues.reserve(params.size());
    paramLengths.reserve(params.size());
    paramFormats.reserve(params.size());
    stringParams.reserve(params.size());

    for (const auto& param : params) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                paramValues.push_back(nullptr);
                paramLengths.push_back(0);
                paramFormats.push_back(0);
            } else {
                stringParams.push_back(valueToString(param));
                paramValues.push_back(stringParams.back().c_str());
                paramLengths.push_back(static_cast<int>(stringParams.back().length()));
                paramFormats.push_back(0);
            }
        }, param);
    }

    // 执行预处理查询
    PGresult* result = PQexecParams(
        m_connection,
        sql.c_str(),
        static_cast<int>(params.size()),
        nullptr,
        paramValues.data(),
        paramLengths.data(),
        paramFormats.data(),
        0
    );

    if (!result) {
        throw PostgreSQLException("PQexecParams failed", m_connection);
    }

    // 使用 RAII 管理 PGresult
    std::unique_ptr<PGresult, decltype(&PQclear)> resultGuard(result, PQclear);

    ExecStatusType status = PQresultStatus(result);
    
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        throw PostgreSQLException("PostgreSQL prepared query failed", result);
    }

    // 如果是没有结果集的命令，返回空结果
    if (status == PGRES_COMMAND_OK) {
        return {};
    }

    DbResult dbResult;
    int numRows = PQntuples(result);
    int numFields = PQnfields(result);

    // 逐行获取数据
    for (int row = 0; row < numRows; ++row) {
        DbRow dbRow;
        dbRow.reserve(numFields);

        for (int col = 0; col < numFields; ++col) {
            if (PQgetisnull(result, row, col)) {
                dbRow.emplace_back(nullptr);
            } else {
                char* value = PQgetvalue(result, row, col);
                // TODO: 根据字段类型进行更精确的转换
                // Oid fieldType = PQftype(result, col);
                dbRow.emplace_back(std::string(value));
            }
        }
        dbResult.push_back(std::move(dbRow));
    }

    return dbResult;
}

} // namespace db
