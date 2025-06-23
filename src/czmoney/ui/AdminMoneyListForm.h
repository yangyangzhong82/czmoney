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

// 前向声明 AdminMoneyEditForm，避免循环依赖
class AdminMoneyEditForm;

class AdminMoneyListForm : public ll::form::CustomForm {
public:
    // 构造函数，接收 Player 引用，可选的搜索过滤器，当前页码和选定的货币类型
    explicit AdminMoneyListForm(Player& player, const std::string& searchFilter = "", int page = 0, const std::string& selectedCurrency = "");

private:
    Player& mPlayer;
    std::string mSearchFilter;
    int mCurrentPage;
    std::string mSelectedCurrency;
    std::vector<ll::service::PlayerInfo::PlayerInfoEntry> mAllPlayers;
    std::vector<ll::service::PlayerInfo::PlayerInfoEntry> mFilteredPlayers;
    std::vector<std::string> mAvailableCurrencies;

    // 辅助函数，用于筛选玩家列表
    void filterPlayers();

public:
    // 获取总页数
    int getTotalPages() const;
};

// 辅助函数，用于创建和显示玩家列表管理表单
void showAdminMoneyListForm(Player& player, const std::string& searchFilter = "", int page = 0, const std::string& selectedCurrency = "");

} // namespace czmoney::ui
