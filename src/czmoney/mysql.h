#pragma once

#include "czmoney/database_interface.h" // 包含数据库接口
#include <mysql.h>
#include <string>
#include <memory>
#include <stdexcept>

namespace db {

/**
 * @brief 自定义 MySQL 异常类
 *
 * 用于封装和报告 MySQL 操作中发生的错误。
 * 继承自 DatabaseException。
 */
class MySQLException : public DatabaseException {
public:
    /**
     * @brief 构造函数 (用于连接或常规错误)
     * @param message 自定义的错误消息
     * @param connection 可选的 MYSQL 连接指针，用于获取 mysql_error() 信息
     */
    explicit MySQLException(const std::string& message, MYSQL* connection = nullptr)
        : DatabaseException(message + (connection ? ": " + std::string(mysql_error(connection)) : "")) {}

    /**
     * @brief 构造函数 (用于预处理语句错误)
     * @param message 自定义的错误消息
     * @param stmt MYSQL_STMT 语句句柄指针，用于获取 mysql_stmt_error() 信息
     */
    explicit MySQLException(const std::string& message, MYSQL_STMT* stmt)
        : DatabaseException(message + ": " + std::string(mysql_stmt_error(stmt))) {}
};


/**
 * @brief 封装 MySQL 数据库连接的类
 *
 * 提供连接、断开、执行查询等基本操作，并管理连接生命周期。
 * 实现 IDatabaseConnection 接口。
 */
class MySQLConnection : public IDatabaseConnection {
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
     * @throws MySQLException (继承自 DatabaseException) 如果连接过程中发生 MySQL 错误
     */
    bool connect() override;

    /**
     * @brief 断开数据库连接
     *
     * 如果当前已连接，则关闭连接。
     */
    void disconnect() override;

    /**
     * @brief 检查当前是否已连接到数据库
     * @return bool 如果已连接返回 true，否则返回 false
     */
    bool isConnected() const override;

    /**
     * @brief 执行一个简单的 SQL 查询语句
     *
     * 主要用于执行 INSERT, UPDATE, DELETE, CREATE 等操作。
     * @param sql 要执行的 SQL 语句字符串
     * @return int 通常 0 表示成功，非 0 表示失败 (MySQL 错误码)
     * @throws MySQLException (继承自 DatabaseException) 如果执行过程中发生 MySQL 错误
     */
    int execute(const std::string& sql) override;

    /**
     * @brief 执行一个返回结果集的 SQL 查询语句 (例如 SELECT)。
     * @param sql 要执行的 SQL 查询语句。
     * @return DbResult 包含查询结果的二维向量。如果查询失败或无结果，可能返回空向量。
     * @throws MySQLException (继承自 DatabaseException) 执行过程中发生 MySQL 错误。
     */
    DbResult query(const std::string& sql) override; // 新增 query 方法声明

    /**
     * @brief 获取底层的 MYSQL C API 连接指针
     *
     * 允许直接访问底层的 MySQL C API 功能，但应谨慎使用，
     * 因为这绕过了本类的封装。
     * @return MYSQL* 指向底层 MYSQL 对象的指针，如果未连接则为 nullptr
     */
    MYSQL* getMYSQL() const;

    /**
     * @brief 获取数据库类型。
     * @return std::string 返回 "mysql"。
     */
    std::string getDbType() const override;

    // --- 事务管理 ---
    void beginTransaction() override;
    void commitTransaction() override;
    void rollbackTransaction() override;

    // --- 预处理语句 ---
    int executePrepared(const std::string& sql, const DbParams& params) override;
    DbResult queryPrepared(const std::string& sql, const DbParams& params) override;

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
