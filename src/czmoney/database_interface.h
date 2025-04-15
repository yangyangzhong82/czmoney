#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <variant> // 用于表示结果中不同的数据类型
#include <optional> // 用于可选结果

namespace db {

// 定义数据库操作可能抛出的通用异常类型
class DatabaseException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error; // 继承构造函数
};

// 定义查询结果的数据类型别名
using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string>;
using DbRow = std::vector<DbValue>;
using DbResult = std::vector<DbRow>;


/**
 * @brief 数据库连接接口 (Interface)
 *
 * 定义了与数据库交互所需的通用操作。
 * MySQLConnection 和 SQLiteConnection 都将实现这个接口。
 */
class IDatabaseConnection {
public:
    // 虚析构函数是必须的，以确保派生类对象能被正确销毁
    virtual ~IDatabaseConnection() = default;

    /**
     * @brief 连接到数据库。
     * @return 如果连接成功返回 true，否则返回 false。
     * @throws DatabaseException 连接过程中发生错误时抛出。
     */
    virtual bool connect() = 0;

    /**
     * @brief 断开与数据库的连接。
     */
    virtual void disconnect() = 0;

    /**
     * @brief 检查当前是否已连接到数据库。
     * @return 如果已连接返回 true，否则返回 false。
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief 执行一个不返回结果集的 SQL 语句 (例如 INSERT, UPDATE, DELETE, CREATE)。
     * @param sql 要执行的 SQL 语句。
     * @return 一个整数，通常表示操作影响的行数或成功/失败状态 (具体含义依赖于实现)。
     * @throws DatabaseException 执行过程中发生错误时抛出。
     */
    virtual int execute(const std::string& sql) = 0;

    /**
     * @brief 执行一个返回结果集的 SQL 查询语句 (例如 SELECT)。
     * @param sql 要执行的 SQL 查询语句。
     * @return DbResult 包含查询结果的二维向量。如果查询失败或无结果，可能返回空向量。
     * @throws DatabaseException 执行过程中发生错误时抛出。
     */
    virtual DbResult query(const std::string& sql) = 0; // 新增 query 方法

    /**
     * @brief 获取当前数据库连接的类型。
     * @return std::string 返回数据库类型字符串 (例如 "mysql", "sqlite")。
     */
    virtual std::string getDbType() const = 0; // 新增 getDbType 方法

    // 未来可以根据需要添加更多方法，例如：
    // - 用于处理预处理语句 (prepare, bind, execute)
    // - 用于事务管理 (beginTransaction, commit, rollback)
};

} // namespace db
