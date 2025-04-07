#include "mod/MyMod.h"
// #include <sqlite3.h> // 如果不再使用 SQLite，可以移除
#include "ll/api/mod/RegisterHelper.h"
// #include <mysql.h> // mysql.h 已被 mysql.cpp 包含，这里不需要重复包含
#include "ll/api/io/Logger.h" // 修正 Logger 引入路径
#include <stdexcept>          // 为了 std::runtime_error (如果使用异常)
namespace my_mod {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool MyMod::enable() {
    auto& logger = getSelf().getLogger();
    logger.debug("Enabling...");

    // --- 数据库连接 ---
    // TODO: 从配置文件读取数据库连接信息
    const std::string db_host = "127.0.0.1";
    const std::string db_user = "your_username";
    const std::string db_password = "your_password";
    const std::string db_name = "your_database";
    const unsigned int db_port = 3306;

    try {
        mDbConnection = std::make_unique<db::MySQLConnection>(
            db_host,
            db_user,
            db_password,
            db_name,
            db_port
        );

        logger.info("Connecting to database...");
        if (mDbConnection->connect()) {
            logger.info("Database connection successful!");
            // 在这里可以执行一些初始化数据库表的操作
            // 例如: mDbConnection->query("CREATE TABLE IF NOT EXISTS ...");
        } else {
            logger.error("Database connection failed!");
            // 根据需要决定是否阻止插件启用
            // return false;
        }
    } catch (const db::MySQLException& e) {
        logger.error("Database error during connection: {}", e.what());
        // return false; // 连接失败，阻止启用
    } catch (const std::exception& e) {
        logger.error("An unexpected error occurred during database connection: {}", e.what());
        // return false;
    }
    // --- 数据库连接结束 ---


    // 其他启用代码...
    return true;
}

bool MyMod::disable() {
    auto& logger = getSelf().getLogger();
    logger.debug("Disabling...");

    // --- 断开数据库连接 ---
    if (mDbConnection && mDbConnection->isConnected()) {
        logger.info("Disconnecting from database...");
        mDbConnection->disconnect();
        logger.info("Database connection closed.");
    }
    mDbConnection.reset(); // 释放 unique_ptr 管理的对象
    // --- 数据库连接结束 ---


    // 其他禁用代码...
    return true;
}

} // namespace my_mod

LL_REGISTER_MOD(my_mod::MyMod, my_mod::MyMod::getInstance());
