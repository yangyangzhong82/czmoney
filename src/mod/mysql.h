#pragma once // 防止头文件被重复包含
#include "ll/api/io/Logger.h" // 引入 LeviLamina 日志记录器
#include <mysql.h>            // 引入 MySQL C API 头文件
#include <string>             // 使用 std::string
#include <memory>             // 可能用于智能指针 (虽然当前未使用)
#include <stdexcept>          // 使用 std::runtime_error 作为异常基类

namespace db {

/**
 * @brief 自定义 MySQL 异常类
 *
 * 用于封装和报告 MySQL 操作中发生的错误。
 * 继承自 std::runtime_error，可以包含 MySQL 驱动返回的错误信息。
 */
class MySQLException : public std::runtime_error {
public:
    /**
     * @brief 构造函数 (用于连接或常规错误)
     * @param message 自定义的错误消息
     * @param connection 可选的 MYSQL 连接指针，用于获取 mysql_error() 信息
     */
    MySQLException(const std::string& message, MYSQL* connection = nullptr)
        : std::runtime_error(message + (connection ? ": " + std::string(mysql_error(connection)) : "")) {}

    /**
     * @brief 构造函数 (用于预处理语句错误)
     * @param message 自定义的错误消息
     * @param stmt MYSQL_STMT 语句句柄指针，用于获取 mysql_stmt_error() 信息
     */
    MySQLException(const std::string& message, MYSQL_STMT* stmt)
        : std::runtime_error(message + ": " + std::string(mysql_stmt_error(stmt))) {}
};


/**
 * @brief 封装 MySQL 数据库连接的类
 *
 * 提供连接、断开、执行查询等基本操作，并管理连接生命周期。
 */
class MySQLConnection {
public:
    /**
     * @brief 构造函数
     * @param host 数据库主机名或 IP 地址
     * @param user 数据库用户名
     * @param password 数据库密码
     * @param database 要连接的数据库名称
     * @param port 数据库端口号 (默认为 3306)
     */
    MySQLConnection(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int       port = 3306
    );

    /**
     * @brief 析构函数
     *
     * 确保在对象销毁时断开数据库连接。
     */
    ~MySQLConnection();

    // 禁用拷贝构造函数和拷贝赋值运算符，防止连接资源被错误地复制
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;

    // 允许移动构造函数和移动赋值运算符 (C++11)
    // 这允许将连接的所有权从一个对象转移到另一个对象，例如从临时对象返回
    MySQLConnection(MySQLConnection&&) noexcept;
    MySQLConnection& operator=(MySQLConnection&&) noexcept;

    /**
     * @brief 连接到数据库
     * @return bool 如果连接成功返回 true，否则返回 false
     * @throws MySQLException 如果连接过程中发生 MySQL 错误
     */
    bool connect();

    /**
     * @brief 断开数据库连接
     *
     * 如果当前已连接，则关闭连接。
     */
    void disconnect();

    /**
     * @brief 检查当前是否已连接到数据库
     * @return bool 如果已连接返回 true，否则返回 false
     */
    bool isConnected() const;

    /**
     * @brief 执行一个简单的 SQL 查询语句
     *
     * 这个版本不处理查询结果，主要用于执行 INSERT, UPDATE, DELETE, CREATE 等操作。
     * @param sql 要执行的 SQL 语句字符串
     * @return int MySQL C API 的返回值 (通常 0 表示成功，非 0 表示失败)
     * @throws MySQLException 如果执行过程中发生 MySQL 错误
     */
    int query(const std::string& sql);

    /**
     * @brief 获取底层的 MYSQL C API 连接指针
     *
     * 允许直接访问底层的 MySQL C API 功能，但应谨慎使用，
     * 因为这绕过了本类的封装。
     * @return MYSQL* 指向底层 MYSQL 对象的指针，如果未连接则为 nullptr
     */
    MYSQL* getMYSQL() const;


private:
    std::string m_host;       // 数据库主机
    std::string m_user;       // 数据库用户
    std::string m_password;   // 数据库密码
    std::string m_database;   // 数据库名称
    unsigned int m_port;      // 数据库端口
    MYSQL* m_connection;      // 指向 MySQL C API 连接对象的指针
    bool m_connected;         // 标记当前是否已连接
};

} // namespace db
