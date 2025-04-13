#include "czmoney/MyMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/io/Logger.h"
#include "ll/api/Config.h"
#include "czmoney/money.h"
#include "czmoney/command.h" // 包含 command.h
#include <stdexcept>
#include <filesystem>
#include <stdexcept>

namespace czmoney {

// MyMod 类的单例实现
MyMod& MyMod::getInstance() {
    static MyMod instance; // 静态局部变量，保证线程安全（C++11 起）
    return instance;
}

// 插件加载时的逻辑
bool MyMod::load() {
    auto& logger = getSelf().getLogger(); // 获取插件自身的 Logger 实例
    logger.debug("Loading..."); // 输出调试信息

    // --- 配置加载 ---
    // 获取配置文件路径
    mConfigPath = getSelf().getConfigDir() / "config.json";
    logger.info("Configuration path: {}", mConfigPath.string()); // 记录配置文件路径


    try {
        // 尝试从文件加载配置到 getConfig() 返回的配置对象中
        ll::config::loadConfig(getConfig(), mConfigPath);
        logger.info("Configuration loaded/updated."); // 配置加载成功
    } catch (const std::exception& e) {
        // 捕获标准异常
        logger.error("Failed to load configuration: {}. Using default values.", e.what());
    } catch (...) {
        // 捕获未知异常
        logger.error("Failed to load configuration due to an unknown error. Using default values.");
    }

    // 始终保存配置，确保文件存在且包含默认值（如果加载失败）或更新后的值
    if (!ll::config::saveConfig(getConfig(), mConfigPath)) {
        logger.error("Failed to save configuration file!");
        // 根据需要，可以选择返回 false 来阻止插件加载
        // return false;
    }
    // --- 配置加载结束 ---

    return true; // 加载成功
}

// 插件启用时的逻辑
bool MyMod::enable() {
    auto& logger = getSelf().getLogger(); // 获取 Logger 实例
    logger.debug("Enabling..."); // 输出调试信息

    // --- 数据库连接 (使用配置) ---
    try {
        // 使用 getConfig() 获取已加载或默认的配置
        const auto& cfg = getConfig();
        // 记录数据库连接信息（注意：密码不应记录）
        logger.info("Database configuration: host={}, user={}, db={}, port={}",
                    cfg.db_host, cfg.db_user, cfg.db_name, cfg.db_port);

        // 创建 MySQLConnection 实例 (使用 unique_ptr 管理生命周期)
        mDbConnection = std::make_unique<db::MySQLConnection>(
            cfg.db_host,
            cfg.db_user,
            cfg.db_password, // 传递密码
            cfg.db_name,
            cfg.db_port
        );

        logger.info("Connecting to database..."); // 记录尝试连接数据库
        if (mDbConnection->connect()) { // 尝试连接
            logger.info("Database connection successful!"); // 连接成功

            // --- 初始化 MoneyManager ---
            // 创建 MoneyManager 实例，传入数据库连接和配置对象
            mMoneyManager = std::make_unique<MoneyManager>(*mDbConnection, getConfig()); // 传递配置
            logger.info("Initializing money database table..."); // 记录初始化数据表
            if (mMoneyManager->initializeTable()) { // 初始化数据库表
                logger.info("Money database table initialized successfully."); // 初始化成功

                // --- 注册命令 ---
                logger.info("Registering money commands...");
                registerMoneyCommands(); // 在 MoneyManager 初始化成功后注册命令
                logger.info("Money commands registered.");
                // --- 命令注册结束 ---

            } else {
                logger.error("Failed to initialize money database table!"); // 初始化失败
                // 初始化失败，阻止插件启用
                mDbConnection->disconnect(); // 断开数据库连接
                mDbConnection.reset();       // 释放数据库连接对象
                mMoneyManager.reset();       // 释放 MoneyManager 对象
                return false;                // 启用失败
            }
            // --- MoneyManager 初始化结束 ---

        } else {
            logger.error("Database connection failed!"); // 连接失败
            // 根据需要决定是否阻止插件启用
            // return false;
        }
    } catch (const db::MySQLException& e) {
        // 捕获数据库特定异常
        logger.error("Database error during connection: {}", e.what());
        // return false; // 连接失败，阻止启用
    } catch (const std::exception& e) {
        // 捕获其他标准异常
        logger.error("An unexpected error occurred during database connection: {}", e.what());
        // return false;
    }
    // --- 数据库连接结束 ---


    // 其他启用代码... (例如注册命令、监听事件等)
    return true; // 启用成功
}

// 插件禁用时的逻辑
bool MyMod::disable() {
    auto& logger = getSelf().getLogger(); // 获取 Logger 实例
    logger.debug("Disabling..."); // 输出调试信息

    // --- 重置 MoneyManager ---
    // 释放 MoneyManager 实例，确保在数据库连接关闭前或后都可以安全执行
    // 如果 MoneyManager 的析构函数依赖数据库连接，则应在断开连接前 reset
    // 如果不依赖，顺序可以随意
    mMoneyManager.reset();
    logger.info("MoneyManager reset.");
    // --- MoneyManager 重置结束 ---


    // --- 断开数据库连接 ---
    // 检查数据库连接对象是否存在且处于连接状态
    if (mDbConnection && mDbConnection->isConnected()) {
        logger.info("Disconnecting from database..."); // 记录断开连接
        mDbConnection->disconnect(); // 执行断开操作
        logger.info("Database connection closed."); // 确认断开
    }
    mDbConnection.reset(); // 释放 unique_ptr 管理的数据库连接对象
    // --- 数据库连接结束 ---


    // 其他禁用代码... (例如取消注册命令、取消监听事件等)
    return true; // 禁用成功
}

// 实现 getMoneyManager 访问器
// 提供对 MoneyManager 实例的访问
MoneyManager& MyMod::getMoneyManager() {
    if (!mMoneyManager) { // 检查 MoneyManager 是否已初始化
        // 如果未初始化（可能插件未启用或初始化失败），抛出异常
        // 或者使用更具体的异常类型，例如 std::logic_error
        throw std::runtime_error("MoneyManager is not initialized. Is the mod enabled?");
    }
    return *mMoneyManager; // 返回 MoneyManager 的引用
}


} // namespace czmoney

// 使用 LeviLamina 的宏注册插件
// LL_REGISTER_MOD(插件类名, 获取插件实例的函数)
LL_REGISTER_MOD(czmoney::MyMod, czmoney::MyMod::getInstance());
