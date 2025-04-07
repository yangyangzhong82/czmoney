#pragma once

#include "ll/api/mod/NativeMod.h"
#include "mod/mysql.h" // 包含 MySQL 连接头文件
#include <memory>      // 为了 std::unique_ptr
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

private:
    ll::mod::NativeMod& mSelf;
    std::unique_ptr<db::MySQLConnection> mDbConnection; // 数据库连接指针
};

} // namespace my_mod
