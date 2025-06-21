
#pragma once

#include "czmoney/database_interface.h" // 包含数据库接口
#include <libpq-fe.h> // PostgreSQL C API
#include <string>
#include <memory>
#include <stdexcept>

namespace db {

/**
 * @brief 自定义 PostgreSQL 异常类
 *
 * 用于封装和报告 PostgreSQL 操作中发生的错误。
 * 继承自 DatabaseException。
 */
class PostgreSQLException : public DatabaseException {
public:
    /**
     * @brief 构造函数 (用于连接或常规错误)
     * @param message 自定义的错误消息
     * @param connection 可选的 PGconn 连接指针，用于获取 PQerrorMessage() 信息
     */
    explicit PostgreSQLException(const std::string& message, PGconn* connection = nullptr)
        : DatabaseException(message + (connection ? ": " + std::string(PQerrorMessage(connection)) : "")) {}

    /**
     * @brief 构造函数 (用于查询结果错误)
     * @param message 自定义的错误消息
     * @param result PGresult 查询结果指针，用于获取 PQresultErrorMessage() 信息
     */
    explicit PostgreSQLException(const std::string& message, PGresult* result)
        : DatabaseException(message + ": " + std::string(PQresultErrorMessage(result))) {}
};


/**
 * @brief 封装 PostgreSQL 数据库连接的类
 *
 * 提供连接、断开、执行查询等基本操作，并管理连接生命周期。
 * 实现 IDatabaseConnection 接口。
 */
class PostgreSQLConnection : public IDatabaseConnection {
public:
    /**
     * @brief 构造函数
     * @param host 数据库主机名或 IP 地址
     * @param user 数据库用户名
     * @param password 数据库密码
     * @param database 要连接的数据库名称
     * @param port 数据库端口号 (默认为 5432)
     */
    PostgreSQLConnection(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int       port = 5432
    );

    /**
     * @brief 析构函数
     *
     * 确保在对象销毁时断开数据库连接。
     */
    ~PostgreSQLConnection();

    // 禁用拷贝构造函数和拷贝赋值运算符，防止连接资源被错误地复制
    PostgreSQLConnection(const PostgreSQLConnection&) = delete;
    PostgreSQLConnection& operator=(const PostgreSQLConnection&) = delete;

    // 允许移动构造函数和移动赋值运算符 (C++11)
    // 这允许将连接的所有权从一个对象转移到另一个对象，例如从临时对象返回
    PostgreSQLConnection(PostgreSQLConnection&&) noexcept;
    PostgreSQLConnection& operator=(PostgreSQLConnection&&) noexcept;

    /**
     * @brief 连接到数据库
     * @return bool 如果连接成功返回 true，否则返回 false
     * @throws PostgreSQLException (继承自 DatabaseException) 如果连接过程中发生 PostgreSQL 错误
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
     * @return int 通常 0 表示成功，非 0 表示失败
     * @throws PostgreSQLException (继承自 DatabaseException) 如果执行过程中发生 PostgreSQL 错误
     */
    int execute(const std::string& sql) override;

    /**
     * @brief 执行一个返回结果集的 SQL 查询语句 (例如 SELECT)。
     * @param sql 要执行的 SQL 查询语句。
     * @return DbResult 包含查询结果的二维向量。如果查询失败或无结果，可能返回空向量。
     * @throws PostgreSQLException (继承自 DatabaseException) 执行过程中发生 PostgreSQL 错误。
     */
    DbResult query(const std::string& sql) override;

    /**
     * @brief 获取底层的 PGconn C API 连接指针
     *
     * 允许直接访问底层的 PostgreSQL C API 功能，但应谨慎使用，
     * 因为这绕过了本类的封装。
     * @return PGconn* 指向底层 PGconn 对象的指针，如果未连接则为 nullptr
     */
    PGconn* getPGconn() const;

    /**
     * @brief 获取数据库类型。
     * @return std::string 返回 "postgresql"。
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
    std::string m_host;         // 数据库主机
    std::string m_user;         // 数据库用户
    std::string m_password;     // 数据库密码
    std::string m_database;     // 数据库名称
    unsigned int m_port;        // 数据库端口
    PGconn* m_connection;       // 指向 PostgreSQL C API 连接对象的指针
    bool m_connected;           // 标记当前是否已连接

    /**
     * @brief 辅助函数：检查查询结果状态并抛出异常（如果需要）
     * @param result PGresult 指针
     * @param expectedStatus 期望的结果状态
     * @param errorMessage 错误消息前缀
     */
    void checkResult(PGresult* result, ExecStatusType expectedStatus, const std::string& errorMessage);

    /**
     * @brief 辅助函数：将 DbValue 转换为 PostgreSQL 参数字符串
     * @param value DbValue 值
     * @return std::string 转换后的字符串表示
     */
    std::string valueToString(const DbValue& value);
};

} // namespace db
