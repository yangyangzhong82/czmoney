#pragma once

#include "ll/api/mod/NativeMod.h"
#include "mod/mysql.h" // 包含 MySQL 连接头文件
#include "mod/config.h"
#include "mod/money.h" // 包含 MoneyManager 头文件
#include <memory>      // 为了 std::unique_ptr
#include <filesystem> // 为了 std::filesystem

namespace my_mod {

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
    std::unique_ptr<db::MySQLConnection> mDbConnection; // 数据库连接指针
    std::unique_ptr<MoneyManager> mMoneyManager; // 经济管理器指针
    Config mConfig; // 存储加载的配置
    std::filesystem::path mConfigPath; // 配置文件路径
};

} // namespace my_mod
