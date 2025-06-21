#include "czmoney/MyMod.h"
#include "czmoney/command.h" // 包含 command.h
#include "czmoney/money/money.h"
#include "czmoney/money/money_api.h"
#include "ll/api/Config.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"
#include <RemoteCallAPI.h>
#include <filesystem>
#include <stdexcept>

namespace czmoney {


MyMod& MyMod::getInstance() {
    static MyMod instance; 
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
        logger.error("Failed to load configuration: {}. Using default values.", e.what());
    } catch (...) {
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
    auto& logger = getSelf().getLogger();
    logger.debug("Enabling...");

    // --- 数据库连接 ---
    try {
        const auto& cfg = getConfig();
        logger.info("Selected database type: {}", cfg.db_type);

        if (cfg.db_type == "mysql") {
            logger.info("Using MySQL database: host={}, user={}, db={}, port={}",
                        cfg.db_host, cfg.db_user, cfg.db_name, cfg.db_port);
            // 创建 MySQLConnection 实例
            mDbConnection = std::make_unique<db::MySQLConnection>(
                cfg.db_host,
                cfg.db_user,
                cfg.db_password,
                cfg.db_name,
                cfg.db_port
            );
        } else if (cfg.db_type == "postgresql") {
            logger.info("Using PostgreSQL database: host={}, user={}, db={}, port={}",
                        cfg.db_pg_host, cfg.db_pg_user, cfg.db_pg_name, cfg.db_pg_port);
            // 创建 PostgreSQLConnection 实例
            mDbConnection = std::make_unique<db::PostgreSQLConnection>(
                cfg.db_pg_host,
                cfg.db_pg_user,
                cfg.db_pg_password,
                cfg.db_pg_name,
                cfg.db_pg_port
            );
        } else if (cfg.db_type == "sqlite") {
            // 获取插件数据目录的绝对路径
            std::filesystem::path dataPath = getSelf().getDataDir();
            // 拼接 SQLite 文件路径
            std::filesystem::path sqlitePath = dataPath / cfg.db_sqlite_path;
             // 确保目录存在
            std::filesystem::create_directories(sqlitePath.parent_path());
            logger.info("Using SQLite database at path: {}", sqlitePath.string());
            // 创建 SQLiteConnection 实例
            mDbConnection = std::make_unique<db::SQLiteConnection>(sqlitePath.string());
        } else {
            logger.error("Unsupported database type configured: {}", cfg.db_type);
            return false;
        }

        logger.info("Connecting to database...");
        if (mDbConnection->connect()) {
            logger.info("Database connection successful!");

            // --- 初始化 MoneyManager ---
            mMoneyManager = std::make_unique<MoneyManager>(*mDbConnection, getConfig());
            logger.info("Initializing money database table...");
            if (mMoneyManager->initializeTable()) {
                logger.info("Money database table initialized successfully.");

                // --- 注册命令 ---
                logger.info("Registering money commands...");
                registerMoneyCommands(getConfig().commandAliases);
                logger.info("Money commands registered.");
                // --- 脚本 API 导出开始 ---
                logger.info("Registering script API functions...");
                RemoteCall::exportAs("czmoney", "getPlayerBalance",
                    std::function<double(std::string, std::string)>(
                        [](std::string uuid, std::string currencyType) -> double {
                            auto opt = ::czmoney::api::getPlayerBalance(uuid, currencyType);
                            return opt.has_value() ? opt.value() : 0.0;
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "getRawPlayerBalance",
                    std::function<int64_t(std::string, std::string)>(
                        [](std::string uuid, std::string currencyType) -> int64_t {
                            auto opt = ::czmoney::api::getRawPlayerBalance(uuid, currencyType);
                            return opt.has_value() ? opt.value() : 0LL;
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "getPlayerBalanceOrInit",
                    std::function<double(std::string, std::string)>(
                        [](std::string uuid, std::string currencyType) -> double {
                            return ::czmoney::api::getPlayerBalanceOrInit(uuid, currencyType);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "getRawPlayerBalanceOrInit",
                    std::function<int64_t(std::string, std::string)>(
                        [](std::string uuid, std::string currencyType) -> int64_t {
                            return ::czmoney::api::getRawPlayerBalanceOrInit(uuid, currencyType);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "setPlayerBalance",
                    std::function<bool(std::string, std::string, double, std::string, std::string, std::string)>(
                        [](std::string uuid, std::string currencyType, double amount,
                           std::string r1, std::string r2, std::string r3) -> bool {
                            return ::czmoney::api::setPlayerBalance(uuid, currencyType, amount, r1, r2, r3);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "addPlayerBalance",
                    std::function<bool(std::string, std::string, double, std::string, std::string, std::string)>(
                        [](std::string uuid, std::string currencyType, double amount,
                           std::string r1, std::string r2, std::string r3) -> bool {
                            return ::czmoney::api::addPlayerBalance(uuid, currencyType, amount, r1, r2, r3);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "subtractPlayerBalance",
                    std::function<bool(std::string, std::string, double, std::string, std::string, std::string)>(
                        [](std::string uuid, std::string currencyType, double amount,
                           std::string r1, std::string r2, std::string r3) -> bool {
                            return ::czmoney::api::subtractPlayerBalance(uuid, currencyType, amount, r1, r2, r3);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "hasAccount",
                    std::function<bool(std::string, std::string)>(
                        [](std::string uuid, std::string currencyType) -> bool {
                            return ::czmoney::api::hasAccount(uuid, currencyType);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "formatBalance",
                    std::function<std::string(int64_t)>(
                        [](int64_t amount) -> std::string {
                            return ::czmoney::api::formatBalance(amount);
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "parseBalance",
                    std::function<int64_t(std::string)>(
                        [](std::string s) -> int64_t {
                            auto opt = ::czmoney::api::parseBalance(s);
                            return opt.has_value() ? opt.value() : 0LL;
                        }
                    )
                );
                RemoteCall::exportAs("czmoney", "transferBalance",
                    std::function<bool(std::string, std::string, std::string, double, std::string, std::string, std::string)>(
                        [](std::string sender, std::string receiver, std::string currencyType, double amount,
                           std::string r1, std::string r2, std::string r3) -> bool {
                            return ::czmoney::api::transferBalance(sender, receiver, currencyType, amount, r1, r2, r3);
                        }
                    )
                );
                logger.info("Script API functions registered.");
                // --- 脚本 API 导出结束 ---
                // --- 命令注册结束 ---

            } else {
                logger.error("Failed to initialize money database table!");
                mDbConnection->disconnect();
                mDbConnection.reset();
                mMoneyManager.reset();
                return false;
            }
            // --- MoneyManager 初始化结束 ---

        } else {
            logger.error("Database connection failed!");
            mDbConnection.reset(); // 释放连接对象
            return false;
        }
    } catch (const db::DatabaseException& e) { // 捕获通用的数据库异常
        logger.error("Database error during initialization: {}", e.what());
        if (mDbConnection) mDbConnection->disconnect(); // 尝试断开连接
        mDbConnection.reset();
        mMoneyManager.reset();
        return false;
    } catch (const std::exception& e) {
        logger.error("An unexpected error occurred during initialization: {}", e.what());
         if (mDbConnection) mDbConnection->disconnect(); // 尝试断开连接
        mDbConnection.reset();
        mMoneyManager.reset();
        return false;
    }
    // --- 数据库连接结束 ---

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


    return true; // 禁用成功
}

// 实现 getMoneyManager 访问器
// 提供对 MoneyManager 实例的访问
MoneyManager& MyMod::getMoneyManager() {
    if (!mMoneyManager) { // 检查 MoneyManager 是否已初始化
        // 如果未初始化（可能插件未启用或初始化失败），抛出异常
        throw std::runtime_error("MoneyManager is not initialized. Is the mod enabled?");
    }
    return *mMoneyManager; // 返回 MoneyManager 的引用
}


} // namespace czmoney

// 使用 LeviLamina 的宏注册插件
// LL_REGISTER_MOD(插件类名, 获取插件实例的函数)
LL_REGISTER_MOD(czmoney::MyMod, czmoney::MyMod::getInstance());
