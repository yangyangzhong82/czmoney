#pragma once

#include "ll/api/mod/NativeMod.h"
#include "czmoney/database_interface.h" // 包含数据库接口
#include "db/mysql.h" // 仍然需要包含，因为 enable 中可能创建 MySQLConnection
#include "db/sqlite.h" // 包含 SQLite 连接头文件
#include "db/postgresql.h" // 包含 PostgreSQL 连接头文件
#include "czmoney/config.h"
#include "czmoney/money/money.h" // 包含 MoneyManager 头文件
#include <memory>      // 为了 std::unique_ptr
#include <filesystem> // 为了 std::filesystem

namespace czmoney {

class MyMod {

public:
    static MyMod& getInstance();

    MyMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    // TODO: Implement this method if you need to unload the mod.
    // /// @return True if the mod is unloaded successfully.
    // bool unload();

    /// @return A reference to the loaded configuration.
    [[nodiscard]] Config& getConfig() { return mConfig; }

    /// @return A reference to the MoneyManager instance.
    /// @warning Throws if the manager is not initialized (mod not enabled).
    [[nodiscard]] MoneyManager& getMoneyManager();


private:
    ll::mod::NativeMod& mSelf;
    std::unique_ptr<db::IDatabaseConnection> mDbConnection; // 使用接口类型的 unique_ptr
    std::unique_ptr<MoneyManager> mMoneyManager; // 经济管理器指针
    Config mConfig; // 存储加载的配置
    std::filesystem::path mConfigPath; // 配置文件路径
};

} // namespace czmoney
