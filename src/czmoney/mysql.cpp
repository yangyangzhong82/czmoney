#include "czmoney/mysql.h"
#include <utility> // For std::move
#include <vector>  // For DbResult, DbRow
#include <variant> // For DbValue

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


} // namespace db
