#include "mod/mysql.h"
#include <utility> // For std::move

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
        // 初始化失败，可以记录日志或抛出异常
        // throw MySQLException("mysql_init failed"); // 示例：抛出异常
        return false;
    }

    // 设置字符集为 utf8mb4 (推荐)
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
        // throw MySQLException("mysql_real_connect failed", m_connection); // 示例：抛出异常
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

// 执行 SQL 查询实现 (简单版本)
int MySQLConnection::query(const std::string& sql) {
    if (!isConnected()) {
        // throw MySQLException("Not connected to database"); // 示例：抛出异常
        return -1; // 或者返回错误码
    }

    if (mysql_query(m_connection, sql.c_str())) {
        // 查询执行失败
        // 可以记录错误: mysql_error(m_connection)
        // throw MySQLException("mysql_query failed", m_connection); // 示例：抛出异常
        return mysql_errno(m_connection); // 返回 MySQL 错误码
    }

    return 0; // 查询成功
}

// 获取底层 MYSQL 指针实现
MYSQL* MySQLConnection::getMYSQL() const {
    return m_connection;
}

} // namespace db
