#pragma once

#include "czmoney/database_interface.h" // 包含数据库接口
#include <sqlite3.h>
#include <string>
#include <stdexcept>
#include <memory>

namespace db {

/**
 * @brief 自定义 SQLite 异常类
 *
 * 用于封装和报告 SQLite 操作中发生的错误。
 * 继承自 DatabaseException。
 */
class SQLiteException : public DatabaseException {
public:
    /**
     * @brief 构造函数
     * @param message 自定义的错误消息
     * @param db 可选的 sqlite3 数据库句柄，用于获取 sqlite3_errmsg() 信息
     */
    explicit SQLiteException(const std::string& message, sqlite3* db = nullptr)
        : DatabaseException(message + (db ? ": " + std::string(sqlite3_errmsg(db)) : "")) {}

     /**
     * @brief 构造函数 (用于语句错误)
     * @param message 自定义的错误消息
     * @param stmt 可选的 sqlite3_stmt 语句句柄，用于获取 sqlite3_errmsg() 信息 (通过 db 句柄)
     */
    explicit SQLiteException(const std::string& message, sqlite3_stmt* stmt)
        : DatabaseException(message + (stmt ? ": " + std::string(sqlite3_errmsg(sqlite3_db_handle(stmt))) : "")) {}
};


/**
 * @brief 封装 SQLite 数据库连接的类
 *
 * 提供连接、断开、执行查询等基本操作，并管理连接生命周期。
 * 实现 IDatabaseConnection 接口。
 */
class SQLiteConnection : public IDatabaseConnection {
public:
    /**
     * @brief 构造函数
     * @param dbPath SQLite 数据库文件的路径
     */
    explicit SQLiteConnection(const std::string& dbPath);

    /**
     * @brief 析构函数
     *
     * 确保在对象销毁时关闭数据库连接。
     */
    ~SQLiteConnection();

    // 禁用拷贝构造函数和拷贝赋值运算符
    SQLiteConnection(const SQLiteConnection&) = delete;
    SQLiteConnection& operator=(const SQLiteConnection&) = delete;

    // 允许移动构造函数和移动赋值运算符
    SQLiteConnection(SQLiteConnection&&) noexcept;
    SQLiteConnection& operator=(SQLiteConnection&&) noexcept;

    /**
     * @brief 连接到数据库 (打开数据库文件)
     * @return bool 如果成功打开返回 true，否则返回 false
     * @throws SQLiteException (继承自 DatabaseException) 如果打开过程中发生 SQLite 错误
     */
    bool connect() override;

    /**
     * @brief 关闭数据库连接
     */
    void disconnect() override;

    /**
     * @brief 检查当前是否已连接到数据库 (数据库文件是否已打开)
     * @return bool 如果已连接返回 true，否则返回 false
     */
    bool isConnected() const override;

    /**
     * @brief 执行一个简单的 SQL 语句 (无结果集)
     *
     * 主要用于执行 INSERT, UPDATE, DELETE, CREATE 等操作。
     * @param sql 要执行的 SQL 语句字符串
     * @return int 通常 SQLITE_OK 表示成功
     * @throws SQLiteException (继承自 DatabaseException) 如果执行过程中发生 SQLite 错误
     */
    int execute(const std::string& sql) override;

    /**
     * @brief 执行一个返回结果集的 SQL 查询语句 (例如 SELECT)。
     * @param sql 要执行的 SQL 查询语句。
     * @return DbResult 包含查询结果的二维向量。如果查询失败或无结果，可能返回空向量。
     * @throws SQLiteException (继承自 DatabaseException) 执行过程中发生 SQLite 错误。
     */
    DbResult query(const std::string& sql) override; // 新增 query 方法声明

    /**
     * @brief 获取底层的 sqlite3 C API 数据库句柄
     *
     * 允许直接访问底层的 SQLite C API 功能，但应谨慎使用。
     * @return sqlite3* 指向底层 sqlite3 对象的指针，如果未连接则为 nullptr
     */
    sqlite3* getDB() const;

    /**
     * @brief 获取数据库类型。
     * @return std::string 返回 "sqlite"。
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
    std::string m_dbPath;     // 数据库文件路径
    sqlite3*    m_db;         // 指向 SQLite C API 数据库对象的指针
    bool        m_connected;  // 标记当前是否已连接
};

} // namespace db
