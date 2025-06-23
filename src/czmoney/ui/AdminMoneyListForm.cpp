#include "czmoney/ui/AdminMoneyListForm.h"
#include "czmoney/MyMod.h"
#include "czmoney/logger.h"
#include "czmoney/money/money_api.h"
#include "czmoney/ui/AdminMoneyEditForm.h" // 引入 AdminMoneyEditForm
#include "ll/api/form/ModalForm.h" // 用于显示提示信息
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include <algorithm>
#include <fmt/format.h>
#include <locale>
#include <string>
#include <variant>
#include <vector>

namespace czmoney::ui {

// 每页显示的玩家数量
constexpr int PLAYERS_PER_PAGE = 8; // 与 TransferForm 保持一致

AdminMoneyListForm::AdminMoneyListForm(Player& player, const std::string& searchFilter, int page, const std::string& selectedCurrency)
    : ll::form::CustomForm("经济管理 - 玩家列表"),
      mPlayer(player),
      mSearchFilter(searchFilter),
      mCurrentPage(page),
      mSelectedCurrency(selectedCurrency)
{
    // 获取所有可用的货币类型
    const auto& config = czmoney::MyMod::getInstance().getConfig();
    for (const auto& pair : config.economy) {
        mAvailableCurrencies.push_back(pair.first);
    }
    // 如果没有可用的货币类型，或者传入的 selectedCurrency 无效，则尝试设置一个默认值
    if (mSelectedCurrency.empty() && !mAvailableCurrencies.empty()) {
        mSelectedCurrency = mAvailableCurrencies[0];
    } else if (!mSelectedCurrency.empty() && std::find(mAvailableCurrencies.begin(), mAvailableCurrencies.end(), mSelectedCurrency) == mAvailableCurrencies.end()) {
        if (!mAvailableCurrencies.empty()) {
            mSelectedCurrency = mAvailableCurrencies[0];
        } else {
            mSelectedCurrency = "money"; // 兜底默认值
        }
    }

    // 获取所有玩家信息
    mAllPlayers.clear();
    for (const auto& entry : ll::service::PlayerInfo::getInstance().entries()) {
        mAllPlayers.push_back(entry);
    }
    // 按照玩家名称排序
    std::sort(mAllPlayers.begin(), mAllPlayers.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    // 根据搜索过滤器筛选玩家
    filterPlayers();

    // 计算总页数
    int totalPages = (mFilteredPlayers.size() + PLAYERS_PER_PAGE - 1) / PLAYERS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;

    // 确保当前页码在有效范围内
    if (mCurrentPage < 0) mCurrentPage = 0;
    if (mCurrentPage >= totalPages) mCurrentPage = totalPages - 1;

    // 添加表单元素
    // 1. 玩家名称模糊搜索输入框
    appendInput("player_search", "玩家名称搜索 (模糊搜索)", "输入玩家名称", mSearchFilter);

    // 2. 货币类型选择下拉列表
    if (mAvailableCurrencies.empty()) {
        appendLabel("§c当前没有可用的经济类型。");
    } else {
        int defaultCurrencyIndex = 0;
        for (size_t i = 0; i < mAvailableCurrencies.size(); ++i) {
            if (mAvailableCurrencies[i] == mSelectedCurrency) {
                defaultCurrencyIndex = i;
                break;
            }
        }
        appendDropdown("currency_type", "选择经济类型", mAvailableCurrencies, defaultCurrencyIndex);
    }

    // 3. 玩家列表按钮
    if (mFilteredPlayers.empty()) {
        appendLabel("§c暂无玩家可供管理");
    } else {
        int startIndex = mCurrentPage * PLAYERS_PER_PAGE;
        int endIndex = std::min(startIndex + PLAYERS_PER_PAGE, (int)mFilteredPlayers.size());
        for (int i = startIndex; i < endIndex; ++i) {
            const auto& playerEntry = mFilteredPlayers[i];
            // 获取玩家当前选定货币的余额
            std::optional<double> balanceOpt = czmoney::api::getPlayerBalance(playerEntry.uuid.asString(), mSelectedCurrency);
            std::string balanceStr = balanceOpt.has_value() ? czmoney::api::formatBalance(static_cast<int64_t>(balanceOpt.value() * 100)) : "N/A";
            
            // 按钮文本：玩家名称 (余额)
            appendToggle(playerEntry.uuid.asString(), fmt::format("{} ({}{})", playerEntry.name, balanceStr, mSelectedCurrency), false);
        }
    }

    // 4. 分页滑块
    appendSlider("page_slider", fmt::format("选择页码: 第{}页", mCurrentPage + 1), 0, totalPages - 1, 1, mCurrentPage);

    // 5. 刷新按钮
    appendToggle("refresh_button", "刷新列表", false);

    // 6. 确认选择按钮
    appendToggle("confirm_selection", "确认选择玩家", false);

    // 7. 返回按钮
    appendToggle("back_button", "返回", false);
}

int AdminMoneyListForm::getTotalPages() const {
    int totalPages = (mFilteredPlayers.size() + PLAYERS_PER_PAGE - 1) / PLAYERS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    return totalPages;
}

void AdminMoneyListForm::filterPlayers() {
    mFilteredPlayers.clear();
    if (mSearchFilter.empty()) {
        mFilteredPlayers = mAllPlayers;
    } else {
        std::string lowerSearchFilter = mSearchFilter;
        std::transform(lowerSearchFilter.begin(), lowerSearchFilter.end(), lowerSearchFilter.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        for (const auto& pInfo : mAllPlayers) {
            std::string lowerPlayerName = pInfo.name;
            std::transform(lowerPlayerName.begin(), lowerPlayerName.end(), lowerPlayerName.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (lowerPlayerName.find(lowerSearchFilter) != std::string::npos) {
                mFilteredPlayers.push_back(pInfo);
            }
        }
    }
}

void showAdminMoneyListForm(Player& player, const std::string& searchFilter, int page, const std::string& selectedCurrency) {
    using ll::form::CustomFormResult;
    using ll::form::FormCancelReason;
    using ll::form::CustomFormElementResult;
    using ll::service::PlayerInfo;

    auto form = std::make_unique<AdminMoneyListForm>(player, searchFilter, page, selectedCurrency);
    int totalPages = form->getTotalPages();

    // 获取所有可用的货币类型 (用于回调中重新构建表单)
    std::vector<std::string> availableCurrenciesForCallback;
    const auto& config = czmoney::MyMod::getInstance().getConfig();
    for (const auto& pair : config.economy) {
        availableCurrenciesForCallback.push_back(pair.first);
    }

    form->sendTo(player, [
        playerUuid = player.getUuid().asString(),
        initialSearchFilter = searchFilter,
        initialPage = page,
        initialSelectedCurrency = selectedCurrency,
        totalPages,
        availableCurrenciesForCallback
    ](Player& player, CustomFormResult const& data, FormCancelReason reason) {
        if (!data.has_value()) {
            logger.debug("经济管理列表表单被玩家 {} 关闭。", player.getRealName());
            return;
        }

        const auto& formData = data.value();

        auto getFormValue = [&](const std::string& key) -> CustomFormElementResult {
            auto it = formData.find(key);
            if (it != formData.end()) {
                return it->second;
            }
            return std::monostate{};
        };

        std::string newSearchFilter = "";
        auto searchFilterResult = getFormValue("player_search");
        if (std::holds_alternative<std::string>(searchFilterResult)) {
            newSearchFilter = std::get<std::string>(searchFilterResult);
        }

        std::string newSelectedCurrency = initialSelectedCurrency;
        auto currencyTypeResult = getFormValue("currency_type");
        if (std::holds_alternative<std::string>(currencyTypeResult)) {
            newSelectedCurrency = std::get<std::string>(currencyTypeResult);
        } else if (std::holds_alternative<uint64>(currencyTypeResult)) {
            size_t selectedIndex = static_cast<size_t>(std::get<uint64>(currencyTypeResult));
            if (selectedIndex < availableCurrenciesForCallback.size()) {
                newSelectedCurrency = availableCurrenciesForCallback[selectedIndex];
            }
        }

        int newPage = 0;
        auto pageSliderResult = getFormValue("page_slider");
        if (std::holds_alternative<double>(pageSliderResult)) {
            newPage = static_cast<int>(std::get<double>(pageSliderResult));
        }

        bool refreshButton = false;
        auto refreshButtonResult = getFormValue("refresh_button");
        if (std::holds_alternative<uint64>(refreshButtonResult)) {
            refreshButton = static_cast<bool>(std::get<uint64>(refreshButtonResult));
        }

        bool backButton = false;
        auto backButtonResult = getFormValue("back_button");
        if (std::holds_alternative<uint64>(backButtonResult)) {
            backButton = static_cast<bool>(std::get<uint64>(backButtonResult));
        }

        // 检查是否点击了返回按钮
        if (backButton) {
            player.sendMessage("§a您退出了经济管理界面。");
            return;
        }

        // 检查是否点击了刷新按钮，或者搜索/分页/货币类型发生变化
        if (refreshButton || newSearchFilter != initialSearchFilter || newPage != initialPage || newSelectedCurrency != initialSelectedCurrency) {
            showAdminMoneyListForm(player, newSearchFilter, newPage, newSelectedCurrency);
            return;
        }

        // 处理玩家按钮点击
        // 遍历所有可能的玩家 UUID，检查哪个按钮被点击
        std::string clickedPlayerUuid = "";
        std::vector<PlayerInfo::PlayerInfoEntry> tempAllPlayers;
        for (const auto& entry : PlayerInfo::getInstance().entries()) {
            tempAllPlayers.push_back(entry);
        }
        std::sort(tempAllPlayers.begin(), tempAllPlayers.end(), [](const auto& a, const auto& b) {
            return a.name < b.name;
        });

        std::vector<PlayerInfo::PlayerInfoEntry> tempFilteredPlayers;
        if (newSearchFilter.empty()) {
            tempFilteredPlayers = tempAllPlayers;
        } else {
            std::string lowerSearchFilter = newSearchFilter;
            std::transform(lowerSearchFilter.begin(), lowerSearchFilter.end(), lowerSearchFilter.begin(),
                           [](unsigned char c){ return std::tolower(c); });

            for (const auto& pInfo : tempAllPlayers) {
                std::string lowerPlayerName = pInfo.name;
                std::transform(lowerPlayerName.begin(), lowerPlayerName.end(), lowerPlayerName.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (lowerPlayerName.find(lowerSearchFilter) != std::string::npos) {
                    tempFilteredPlayers.push_back(pInfo);
                }
            }
        }

        int startIndex = newPage * PLAYERS_PER_PAGE;
        int endIndex = std::min(startIndex + PLAYERS_PER_PAGE, (int)tempFilteredPlayers.size());

        for (int i = startIndex; i < endIndex; ++i) {
            const auto& playerEntry = tempFilteredPlayers[i];
            std::string playerUuidStr = playerEntry.uuid.asString();
            auto buttonResult = getFormValue(playerUuidStr); // 按钮的 name 就是玩家 UUID
            if (std::holds_alternative<uint64>(buttonResult) && static_cast<bool>(std::get<uint64>(buttonResult))) {
                clickedPlayerUuid = playerUuidStr;
                break; // 找到点击的玩家按钮
            }
        }

        bool confirmSelection = false;
        auto confirmSelectionResult = getFormValue("confirm_selection");
        if (std::holds_alternative<uint64>(confirmSelectionResult)) {
            confirmSelection = static_cast<bool>(std::get<uint64>(confirmSelectionResult));
        }

        if (confirmSelection) {
            std::string selectedPlayerUuid = "";
            int selectedCount = 0;

            // 重新获取所有玩家信息以确保名称准确和遍历顺序
            std::vector<PlayerInfo::PlayerInfoEntry> allPlayersForSelectionCheck;
            for (const auto& entry : PlayerInfo::getInstance().entries()) {
                allPlayersForSelectionCheck.push_back(entry);
            }
            std::sort(allPlayersForSelectionCheck.begin(), allPlayersForSelectionCheck.end(), [](const auto& a, const auto& b) {
                return a.name < b.name;
            });

            // 根据当前搜索和分页状态，确定当前页显示的玩家
            std::vector<PlayerInfo::PlayerInfoEntry> currentVisiblePlayers;
            if (newSearchFilter.empty()) {
                currentVisiblePlayers = allPlayersForSelectionCheck;
            } else {
                std::string lowerSearchFilter = newSearchFilter;
                std::transform(lowerSearchFilter.begin(), lowerSearchFilter.end(), lowerSearchFilter.begin(),
                               [](unsigned char c){ return std::tolower(c); });

                for (const auto& pInfo : allPlayersForSelectionCheck) {
                    std::string lowerPlayerName = pInfo.name;
                    std::transform(lowerPlayerName.begin(), lowerPlayerName.end(), lowerPlayerName.begin(),
                                   [](unsigned char c){ return std::tolower(c); });
                    if (lowerPlayerName.find(lowerSearchFilter) != std::string::npos) {
                        currentVisiblePlayers.push_back(pInfo);
                    }
                }
            }

            int startIndex = newPage * PLAYERS_PER_PAGE;
            int endIndex = std::min(startIndex + PLAYERS_PER_PAGE, (int)currentVisiblePlayers.size());

            for (int i = startIndex; i < endIndex; ++i) {
                const auto& playerEntry = currentVisiblePlayers[i];
                std::string playerUuidStr = playerEntry.uuid.asString();
                auto toggleResult = getFormValue(playerUuidStr);
                if (std::holds_alternative<uint64>(toggleResult) && static_cast<bool>(std::get<uint64>(toggleResult))) {
                    selectedPlayerUuid = playerUuidStr;
                    selectedCount++;
                }
            }

            if (selectedCount == 0) {
                player.sendMessage("§c请选择一个玩家进行管理。");
                showAdminMoneyListForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }
            if (selectedCount > 1) {
                player.sendMessage("§c一次只能选择一个玩家进行管理。");
                showAdminMoneyListForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }

            // 找到了唯一选中的玩家
            std::string clickedPlayerName = "未知玩家";
            for (const auto& pInfo : allPlayersForSelectionCheck) {
                if (pInfo.uuid.asString() == selectedPlayerUuid) {
                    clickedPlayerName = pInfo.name;
                    break;
                }
            }
            player.sendMessage(fmt::format("§a您选择了玩家: {}。正在打开其经济管理界面...", clickedPlayerName));
            showAdminMoneyEditForm(player, selectedPlayerUuid, newSelectedCurrency, newSearchFilter, newPage);
            return;
        }

        // 如果搜索过滤器、页码或货币类型发生变化，或者点击了刷新按钮，则重新显示表单
        // 否则，如果只是点击了空白区域或未触发任何操作，也重新显示表单
        showAdminMoneyListForm(player, newSearchFilter, newPage, newSelectedCurrency);
    });
}

} // namespace czmoney::ui
