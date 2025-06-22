#pragma once

#include "ll/api/form/SimpleForm.h" // 修正为 ll/api/form/SimpleForm.h
#include "mc/world/actor/player/Player.h" // 修正为正确的 Player 头文件
#include "ll/api/service/PlayerInfo.h"    // 用于获取离线玩家信息
#include "czmoney/money/money.h"
#include "czmoney/MyMod.h" // 用于获取 MoneyManager 实例

namespace czmoney::ui {
// RankForm 继承自 ll::form::SimpleForm
class RankForm : public ll::form::SimpleForm { // 修正命名空间
public:
    // 构造函数，接收 Player 引用
    explicit RankForm(Player& player);
    
private:
    Player& mPlayer; // 玩家引用


    // 获取玩家名称的辅助函数
    std::string getPlayerName(const std::string& uuid);
};

} // namespace czmoney::ui
