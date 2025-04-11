#include "mod/MyMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/io/Logger.h"
#include "ll/api/Config.h" // 包含配置 API 头文件
#include <stdexcept>
#include <filesystem> // 确保包含 filesystem

namespace my_mod {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    auto& logger = getSelf().getLogger();
    logger.debug("Loading...");

    //加载配置
    mConfigPath = getSelf().getConfigDir() / "config.json";
    logger.info("Configuration path: {}", mConfigPath.string());


    try {
        ll::config::loadConfig(getConfig(), mConfigPath);
        logger.info("Configuration loaded/updated.");
    } catch (const std::exception& e) {
        logger.error("Failed to load configuration: {}. Using default values.", e.what());
    } catch (...) {
        logger.error("Failed to load configuration due to an unknown error. Using default values.");
    }

    // 始终保存配置，确保文件存在且格式正确
    if (!ll::config::saveConfig(getConfig(), mConfigPath)) {
        logger.error("Failed to save configuration file!");
        // return false;
    }

    return true;
}

bool MyMod::enable() {
    auto& logger = getSelf().getLogger();
    logger.debug("Enabling...");

    // --- 数据库连接 (使用配置) ---
    try {
        // 使用 getConfig() 获取配置
        const auto& cfg = getConfig();
        logger.info("Database configuration: host={}, user={}, db={}, port={}",
                    cfg.db_host, cfg.db_user, cfg.db_name, cfg.db_port);

        mDbConnection = std::make_unique<db::MySQLConnection>(
            cfg.db_host,
            cfg.db_user,
            cfg.db_password, // 注意：密码通常不应记录在日志中
            cfg.db_name,
            cfg.db_port
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
