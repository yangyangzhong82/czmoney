#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <variant> // 用于表示结果中不同的数据类型
#include <optional> // 用于可选结果
#include <vector>   // 用于绑定参数

namespace db {

// 定义数据库操作可能抛出的通用异常类型
class DatabaseException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error; // 继承构造函数
};

// 定义查询结果的数据类型别名
using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string>; // 参数和结果值类型
using DbRow = std::vector<DbValue>;     // 单行结果
using DbResult = std::vector<DbRow>;    // 多行结果集
using DbParams = std::vector<DbValue>;  // 用于绑定预处理语句的参数列表


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
    virtual DbResult query(const std::string& sql) = 0;

    /**
     * @brief 获取当前数据库连接的类型。
     * @return std::string 返回数据库类型字符串 (例如 "mysql", "sqlite")。
     */
    virtual std::string getDbType() const = 0;

    // --- 事务管理 ---

    /**
     * @brief 开始一个数据库事务。
     * @throws DatabaseException 如果开始事务失败。
     */
    virtual void beginTransaction() = 0;

    /**
     * @brief 提交当前事务。
     * @throws DatabaseException 如果提交事务失败。
     */
    virtual void commitTransaction() = 0;

    /**
     * @brief 回滚当前事务。
     * @throws DatabaseException 如果回滚事务失败。
     */
    virtual void rollbackTransaction() = 0;

    // --- 预处理语句 (Prepared Statements) ---
    // 注意：这是一个简化的接口，实际实现可能更复杂。
    // 这里不显式返回句柄，而是假设实现类内部管理。

    /**
     * @brief 执行一个带参数的、不返回结果集的 SQL 语句 (预处理方式)。
     * @param sql 带占位符 (?) 的 SQL 语句。
     * @param params 按顺序绑定的参数列表。
     * @return 一个整数，通常表示操作影响的行数或成功/失败状态。
     * @throws DatabaseException 执行过程中发生错误时抛出。
     */
    virtual int executePrepared(const std::string& sql, const DbParams& params) = 0;

    /**
     * @brief 执行一个带参数的、返回结果集的 SQL 查询语句 (预处理方式)。
     * @param sql 带占位符 (?) 的 SQL 查询语句。
     * @param params 按顺序绑定的参数列表。
     * @return DbResult 包含查询结果的二维向量。
     * @throws DatabaseException 执行过程中发生错误时抛出。
     */
    virtual DbResult queryPrepared(const std::string& sql, const DbParams& params) = 0;
};

} // namespace db
