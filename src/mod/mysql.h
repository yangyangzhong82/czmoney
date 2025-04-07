#pragma once

#include <mysql.h>
#include <string>
#include <memory>
#include <stdexcept> // For std::runtime_error

namespace db {

// 自定义异常类，用于报告数据库错误
class MySQLException : public std::runtime_error {
public:
    MySQLException(const std::string& message, MYSQL* connection = nullptr)
        : std::runtime_error(message + (connection ? ": " + std::string(mysql_error(connection)) : "")) {}

    MySQLException(const std::string& message, MYSQL_STMT* stmt)
        : std::runtime_error(message + ": " + std::string(mysql_stmt_error(stmt))) {}
};


class MySQLConnection {
public:
    // 构造函数，接收连接参数
    MySQLConnection(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int       port = 3306
    );

    // 析构函数，确保连接被关闭
    ~MySQLConnection();

    // 禁用拷贝构造和拷贝赋值
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;

    // 允许移动构造和移动赋值 (可选，但通常有益)
    MySQLConnection(MySQLConnection&&) noexcept;
    MySQLConnection& operator=(MySQLConnection&&) noexcept;

    // 连接到数据库
    bool connect();

    // 断开数据库连接
    void disconnect();

    // 检查是否已连接
    bool isConnected() const;

    // 执行 SQL 查询 (简单版本，后续可扩展)
    // 返回值: 0 表示成功，非 0 表示失败
    int query(const std::string& sql);

    // 获取底层的 MYSQL 指针 (谨慎使用)
    MYSQL* getMYSQL() const;

private:
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_database;
    unsigned int m_port;
    MYSQL* m_connection; // 指向 MySQL 连接对象的指针
    bool m_connected;    // 连接状态标志
};

} // namespace db
