#include "czmoney/sqlite.h"
#include <utility> // For std::move
#include <filesystem> // For creating directories if needed
#include <vector> // For DbResult and DbRow
#include <variant> // For DbValue

namespace db {

// RAII 辅助类，用于自动管理 sqlite3_stmt 的生命周期
class SQLiteStatementGuard {
    sqlite3_stmt* mStmt; // 指向 SQLite 语句句柄的指针
public:
    // 构造函数：获取语句句柄
    explicit SQLiteStatementGuard(sqlite3_stmt* stmt = nullptr) : mStmt(stmt) {}
    // 析构函数：如果句柄有效，则调用 sqlite3_finalize
    ~SQLiteStatementGuard() {
        if (mStmt) {
            sqlite3_finalize(mStmt);
        }
    }
    // 禁用拷贝构造函数
    SQLiteStatementGuard(const SQLiteStatementGuard&) = delete;
    // 禁用拷贝赋值运算符
    SQLiteStatementGuard& operator=(const SQLiteStatementGuard&) = delete;
    // 允许移动构造函数
    SQLiteStatementGuard(SQLiteStatementGuard&& other) noexcept : mStmt(other.mStmt) {
        other.mStmt = nullptr; // 将源对象的指针置空，防止重复释放
    }
    // 允许移动赋值运算符
    SQLiteStatementGuard& operator=(SQLiteStatementGuard&& other) noexcept {
        if (this != &other) { // 防止自赋值
            if (mStmt) sqlite3_finalize(mStmt); // 释放当前持有的资源
            mStmt = other.mStmt; // 获取源对象的资源
            other.mStmt = nullptr; // 将源对象的指针置空
        }
        return *this;
    }
    // 获取原始的语句句柄指针
    sqlite3_stmt* get() const { return mStmt; }
    // 释放所有权（用于手动管理或转移）
    sqlite3_stmt* release() {
        sqlite3_stmt* temp = mStmt;
        mStmt = nullptr;
        return temp;
    }
    // 重置（释放当前句柄并接管新句柄）
    void reset(sqlite3_stmt* stmt = nullptr) {
         if (mStmt) {
            sqlite3_finalize(mStmt);
         }
         mStmt = stmt;
    }
};

// 构造函数实现
SQLiteConnection::SQLiteConnection(const std::string& dbPath) :
    m_dbPath(dbPath),
    m_db(nullptr),
    m_connected(false) {}

// 析构函数实现
SQLiteConnection::~SQLiteConnection() {
    disconnect(); // 确保在对象销毁时关闭连接
}

// 移动构造函数实现
SQLiteConnection::SQLiteConnection(SQLiteConnection&& other) noexcept :
    m_dbPath(std::move(other.m_dbPath)),
    m_db(other.m_db), // 转移指针所有权
    m_connected(other.m_connected) {
    // 将源对象的指针置空，防止其析构函数关闭连接
    other.m_db = nullptr;
    other.m_connected = false;
}

// 移动赋值运算符实现
SQLiteConnection& SQLiteConnection::operator=(SQLiteConnection&& other) noexcept {
    if (this != &other) { // 防止自赋值
        // 先释放当前对象的资源
        disconnect();

        // 移动源对象的资源
        m_dbPath = std::move(other.m_dbPath);
        m_db = other.m_db; // 转移指针所有权
        m_connected = other.m_connected;

        // 将源对象的指针置空
        other.m_db = nullptr;
        other.m_connected = false;
    }
    return *this;
}

// 连接数据库实现 (打开数据库文件)
bool SQLiteConnection::connect() {
    if (m_connected) {
        return true; // 已经连接
    }

    // 尝试打开数据库文件。如果文件不存在，SQLite 会尝试创建它。
    // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE: 读写模式打开，如果不存在则创建
    // SQLITE_OPEN_FULLMUTEX: 启用完整的互斥锁，确保线程安全 (根据需要选择)
    int rc = sqlite3_open_v2(m_dbPath.c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);

    if (rc != SQLITE_OK) {
        // 打开失败，记录错误信息并抛出异常
        std::string errMsg = "sqlite3_open_v2 failed for path: " + m_dbPath;
        // 如果 m_db 不为 nullptr，可以获取更详细的错误信息
        if (m_db) {
             errMsg += ": " + std::string(sqlite3_errmsg(m_db));
             sqlite3_close(m_db); // 关闭句柄
             m_db = nullptr;
        } else {
             // 如果 m_db 为 nullptr (例如内存不足)，sqlite3_errmsg 不可用
             errMsg += " (possibly out of memory)";
        }
        throw SQLiteException(errMsg);
        return false; // 虽然抛出异常，但保持返回类型一致性
    }

    // 连接成功，先设置状态
    m_connected = true; 
    m_db = m_db; // 确保 m_db 有效

    // 启用外键约束 (推荐) - 现在 isConnected() 会返回 true
    try {
        execute("PRAGMA foreign_keys = ON;");
    } catch (const SQLiteException& e) {
        // 如果设置 PRAGMA 失败，记录警告但连接本身仍然是成功的
        // (可以选择更严格地处理，例如抛出异常或返回 false)
        // mLogger is not available here, need to handle differently if logging is desired
        // For now, just ignore the exception for PRAGMA setting
        // std::cerr << "Warning: Failed to enable foreign keys: " << e.what() << std::endl;
    }

    return true;
}

// 关闭数据库连接实现
void SQLiteConnection::disconnect() {
    if (m_db) {
        sqlite3_close(m_db); // 关闭数据库连接
        m_db = nullptr;
        m_connected = false;
    }
}

// 检查连接状态实现
bool SQLiteConnection::isConnected() const {
    return m_connected && m_db;
}

// 执行 SQL 语句实现 (简单版本，无结果集)
int SQLiteConnection::execute(const std::string& sql) {
    if (!isConnected()) {
        throw SQLiteException("Not connected to SQLite database");
        // return SQLITE_ERROR; // 或者返回错误码
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string errorStr = "sqlite3_exec failed: " + sql;
        if (errMsg) {
            errorStr += " - Error: " + std::string(errMsg);
            sqlite3_free(errMsg); // 释放 SQLite 分配的错误消息内存
        }
        throw SQLiteException(errorStr, m_db); // 抛出异常
        // return rc; // 返回 SQLite 错误码
    }

    return rc; // 返回 SQLITE_OK
}


// --- SQLiteConnection::query 实现 ---
DbResult SQLiteConnection::query(const std::string& sql) {
    if (!isConnected()) {
        throw SQLiteException("Not connected to SQLite database");
    }

    sqlite3_stmt* stmt = nullptr;
    // 准备 SQL 语句
    // sqlite3_prepare_v2(db, sql, nByte, ppStmt, pzTail)
    // nByte = -1 表示 sql 字符串以 null 结尾
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    SQLiteStatementGuard stmtGuard(stmt); // 使用 RAII 管理 stmt

    if (rc != SQLITE_OK) {
        // 准备失败，stmt 可能为 nullptr 或包含错误信息
        throw SQLiteException("sqlite3_prepare_v2 failed for SQL: " + sql, m_db);
    }

    DbResult result; // 存储查询结果

    // 逐步执行语句并获取行
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        DbRow row; // 存储当前行的数据
        int columnCount = sqlite3_column_count(stmt);
        row.reserve(columnCount); // 预分配空间

        for (int i = 0; i < columnCount; ++i) {
            int colType = sqlite3_column_type(stmt, i);
            switch (colType) {
                case SQLITE_INTEGER:
                    row.emplace_back(sqlite3_column_int64(stmt, i));
                    break;
                case SQLITE_FLOAT:
                    row.emplace_back(sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                    // sqlite3_column_text 返回 const unsigned char*，需要转换为 std::string
                    row.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i)));
                    break;
                case SQLITE_BLOB:
                    // BLOB 数据暂不支持，可以根据需要添加处理逻辑
                    // 例如，可以将其作为 std::vector<unsigned char> 或 std::string 处理
                    // 这里暂时将其视为空字符串或抛出异常
                    row.emplace_back(std::string("[BLOB data]")); // 或者抛出异常
                    // throw SQLiteException("BLOB data type not supported in query", stmt);
                    break;
                case SQLITE_NULL:
                    row.emplace_back(nullptr); // 使用 std::nullptr_t 表示 NULL
                    break;
                default:
                    // 未知类型
                     row.emplace_back(std::string("[Unknown data type]"));
                    // throw SQLiteException("Unknown column data type encountered", stmt);
                    break;
            }
        }
        result.push_back(std::move(row)); // 将行添加到结果集
    }

    // 检查 sqlite3_step 的最终状态
    if (rc != SQLITE_DONE) {
        // 如果不是 SQLITE_DONE，说明执行过程中发生了错误
        throw SQLiteException("sqlite3_step failed during query execution", stmt);
    }

    // stmt 会在 stmtGuard 析构时自动调用 sqlite3_finalize
    return result;
}


// 获取底层 sqlite3 句柄实现
sqlite3* SQLiteConnection::getDB() const {
    return m_db;
}

// 实现 getDbType 方法
std::string SQLiteConnection::getDbType() const {
    return "sqlite";
}

} // namespace db
