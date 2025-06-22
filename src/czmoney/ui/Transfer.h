#pragma once

#include "ll/api/form/CustomForm.h" // 用于自定义表单
#include "mc/world/actor/player/Player.h" // 用于玩家对象
#include "ll/api/service/PlayerInfo.h"    // 用于获取离线玩家信息
#include "mc/platform/UUID.h"             // 用于 UUID 转换
#include "czmoney/money/money_api.h"      // 用于转账API
#include "czmoney/MyMod.h"                // 用于获取日志和配置

#include <string>
#include <vector>
#include <optional>
#include <algorithm> // For std::sort
#include <unordered_map> // For std::unordered_map

namespace czmoney::ui {

class TransferForm : public ll::form::CustomForm {
public:
    // 构造函数，接收 Player 引用，可选的搜索过滤器，当前页码和选定的货币类型
    explicit TransferForm(Player& player, const std::string& searchFilter = "", int page = 0, const std::string& selectedCurrency = "");

private:
    Player& mPlayer; // 发起转账的玩家引用
    std::string mSearchFilter; // 当前搜索关键词
    int mCurrentPage;          // 当前页码
    std::string mSelectedCurrency; // 当前选定的货币类型
    std::vector<ll::service::PlayerInfo::PlayerInfoEntry> mAllPlayers; // 所有玩家信息
    std::vector<ll::service::PlayerInfo::PlayerInfoEntry> mFilteredPlayers; // 筛选后的玩家信息
    std::vector<std::string> mAvailableCurrencies; // 所有可用的货币类型名称

    // 获取玩家名称的辅助函数
    std::string getPlayerName(const std::string& uuid);

    // 获取所有玩家信息并进行筛选的辅助函数
    void filterPlayers();

public:
    // 获取总页数
    int getTotalPages() const;
};

// 辅助函数，用于创建和显示转账表单
void showTransferForm(Player& player, const std::string& searchFilter = "", int page = 0, const std::string& selectedCurrency = "");

} // namespace czmoney::ui
