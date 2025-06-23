#pragma once

#include "ll/api/form/CustomForm.h"
#include "mc/world/actor/player/Player.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "czmoney/money/money_api.h"
#include "czmoney/MyMod.h"

#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <unordered_map>

namespace czmoney::ui {

class AdminMoneyEditForm : public ll::form::CustomForm {
public:
    // 构造函数，接收当前玩家、目标玩家UUID、初始选定货币类型，以及返回列表表单时的状态
    explicit AdminMoneyEditForm(
        Player& player,
        const std::string& targetPlayerUuid,
        const std::string& initialCurrency = "",
        const std::string& returnSearchFilter = "",
        int returnPage = 0
    );

private:
    Player& mPlayer;
    std::string mTargetPlayerUuid;
    std::string mTargetPlayerName; // 缓存目标玩家名称
    std::string mSelectedCurrency;
    std::vector<std::string> mAvailableCurrencies;

    // 用于返回列表表单时保留状态
    std::string mReturnSearchFilter;
    int mReturnPage;

    // 辅助函数，获取玩家名称
    std::string getPlayerName(const std::string& uuid);

public:
    // 获取目标玩家UUID
    const std::string& getTargetPlayerUuid() const { return mTargetPlayerUuid; }
    // 获取目标玩家名称
    const std::string& getTargetPlayerName() const { return mTargetPlayerName; }
};

// 辅助函数，用于创建和显示玩家经济编辑表单
void showAdminMoneyEditForm(
    Player& player,
    const std::string& targetPlayerUuid,
    const std::string& initialCurrency = "",
    const std::string& returnSearchFilter = "",
    int returnPage = 0
);

} // namespace czmoney::ui
