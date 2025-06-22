#include "czmoney/ui/Transfer.h"
#include "czmoney/MyMod.h"
#include "czmoney/logger.h" // 用于日志记录
#include "czmoney/money/money_api.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/ModalForm.h" // 用于显示转账结果
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include <algorithm> // For std::transform, std::tolower
#include <fmt/format.h>
#include <locale> // For std::tolower
#include <string>
#include <variant> // For std::get
#include <vector>

namespace czmoney::ui {

// 每页显示的玩家数量
constexpr int PLAYERS_PER_PAGE = 8; // 根据图片估算，大约8个玩家选项

TransferForm::TransferForm(Player& player, const std::string& searchFilter, int page, const std::string& selectedCurrency)
    : ll::form::CustomForm("转账操作"),
      mPlayer(player),
      mSearchFilter(searchFilter),
      mCurrentPage(page),
      mSelectedCurrency(selectedCurrency)
{
    // 获取所有可用的货币类型
    const auto& config = czmoney::MyMod::getInstance().getConfig();
    for (const auto& pair : config.economy) {
        // 只有允许转账的货币类型才显示在下拉列表中
        if (pair.second.allowTransfer) {
            mAvailableCurrencies.push_back(pair.first);
        }
    }
    // 如果没有可用的货币类型，或者传入的 selectedCurrency 无效，则尝试设置一个默认值
    if (mSelectedCurrency.empty() && !mAvailableCurrencies.empty()) {
        mSelectedCurrency = mAvailableCurrencies[0];
    } else if (!mSelectedCurrency.empty() && std::find(mAvailableCurrencies.begin(), mAvailableCurrencies.end(), mSelectedCurrency) == mAvailableCurrencies.end()) {
        // 如果传入的 selectedCurrency 不在可用列表中，则重置为第一个可用货币
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
    if (totalPages == 0) totalPages = 1; // 至少一页

    // 确保当前页码在有效范围内
    if (mCurrentPage < 0) mCurrentPage = 0;
    if (mCurrentPage >= totalPages) mCurrentPage = totalPages - 1;

    // 添加表单元素
    // 1. 玩家名称模糊搜索输入框
    appendInput("player_search", "玩家名称搜索 (模糊搜索)", "输入玩家名称", mSearchFilter);

    // 2. 货币类型选择下拉列表
    if (mAvailableCurrencies.empty()) {
        appendLabel("§c当前没有可用的经济类型进行转账。");
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

    // 3. 玩家选择 Toggle 列表
    if (mFilteredPlayers.empty()) {
        appendLabel("§c暂无玩家可供选择");
    } else {
        int startIndex = mCurrentPage * PLAYERS_PER_PAGE;
        int endIndex = std::min(startIndex + PLAYERS_PER_PAGE, (int)mFilteredPlayers.size());
        for (int i = startIndex; i < endIndex; ++i) {
            // 将玩家的 UUID 作为 toggle 的 name
            appendToggle(mFilteredPlayers[i].uuid.asString(), mFilteredPlayers[i].name, false);
        }
    }

    // 4. 转账金额输入框
    appendInput("transfer_amount", "转账金额", "输入转账金额 (例如: 100.50)", "");

    // 5. 分页滑块 (模拟图片中的页码选择)
    appendSlider("page_slider", fmt::format("选择页码: 第{}页", mCurrentPage + 1), 0, totalPages - 1, 1, mCurrentPage);

    // 6. 确认添加按钮 (图片中的“确认添加”，这里用于提交转账)
    appendToggle("confirm_transfer", "确认转账", false);

    // 7. 返回按钮 (图片中的“返回”)
    appendToggle("back_button", "返回", false);
}

// 实现 getTotalPages 方法
int TransferForm::getTotalPages() const {
    int totalPages = (mFilteredPlayers.size() + PLAYERS_PER_PAGE - 1) / PLAYERS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    return totalPages;
}

std::string TransferForm::getPlayerName(const std::string& uuid) {
    using ll::service::PlayerInfo; // 引入命名空间
    auto playerInfoOpt = PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuid));
    if (playerInfoOpt.has_value()) {
        return playerInfoOpt.value().name;
    }
    logger.warn("无法获取 UUID {} 的玩家名称，将显示为 '未知玩家'。", uuid);
    return "未知玩家";
}

void TransferForm::filterPlayers() {
    using ll::service::PlayerInfo; // 引入命名空间
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

void showTransferForm(Player& player, const std::string& searchFilter, int page, const std::string& selectedCurrency) {
    using ll::form::CustomFormResult; // 引入命名空间
    using ll::form::FormCancelReason; // 引入命名空间
    using ll::form::CustomFormElementResult; // 引入命名空间
    using ll::service::PlayerInfo; // 引入命名空间

    auto form = std::make_unique<TransferForm>(player, searchFilter, page, selectedCurrency);
    int totalPages = form->getTotalPages(); // 使用公共方法获取总页数

    // 获取所有可用的货币类型 (用于回调中重新构建表单)
    std::vector<std::string> availableCurrenciesForCallback;
    const auto& config = czmoney::MyMod::getInstance().getConfig();
    for (const auto& pair : config.economy) {
        if (pair.second.allowTransfer) {
            availableCurrenciesForCallback.push_back(pair.first);
        }
    }

    form->sendTo(player, [
        playerUuid = player.getUuid().asString(),
        initialSearchFilter = searchFilter,
        initialPage = page,
        initialSelectedCurrency = selectedCurrency, // 捕获初始选定的货币类型
        totalPages,
        availableCurrenciesForCallback // 捕获可用货币类型列表
    ](Player& player, CustomFormResult const& data, FormCancelReason reason) { // 使用引入的类型
        if (!data.has_value()) {
            logger.debug("转账表单被玩家 {} 关闭。", player.getRealName());
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

        std::string newSelectedCurrency = initialSelectedCurrency; // 默认使用初始值
        auto currencyTypeResult = getFormValue("currency_type");
        if (std::holds_alternative<std::string>(currencyTypeResult)) { // 优先尝试作为字符串获取
            newSelectedCurrency = std::get<std::string>(currencyTypeResult);
        } else if (std::holds_alternative<uint64>(currencyTypeResult)) { // 如果不是字符串，再尝试作为索引获取
            size_t selectedIndex = static_cast<size_t>(std::get<uint64>(currencyTypeResult));
            if (selectedIndex < availableCurrenciesForCallback.size()) {
                newSelectedCurrency = availableCurrenciesForCallback[selectedIndex];
            }
        }

        std::string amountStr = "";
        auto amountStrResult = getFormValue("transfer_amount");
        if (std::holds_alternative<std::string>(amountStrResult)) {
            amountStr = std::get<std::string>(amountStrResult);
        }

        int newPage = 0;
        auto pageSliderResult = getFormValue("page_slider");
        if (std::holds_alternative<double>(pageSliderResult)) {
            newPage = static_cast<int>(std::get<double>(pageSliderResult));
        }

        bool confirmTransfer = false;
        auto confirmTransferResult = getFormValue("confirm_transfer");
        if (std::holds_alternative<uint64>(confirmTransferResult)) {
            confirmTransfer = static_cast<bool>(std::get<uint64>(confirmTransferResult));
        }

        bool backButton = false;
        auto backButtonResult = getFormValue("back_button");
        if (std::holds_alternative<uint64>(backButtonResult)) {
            backButton = static_cast<bool>(std::get<uint64>(backButtonResult));
        }

        logger.debug("TransferForm Callback: formData content:");
        for (const auto& pair : formData) {
            std::string valueStr = "Unknown Type";
            if (std::holds_alternative<std::monostate>(pair.second)) {
                valueStr = "monostate";
            } else if (std::holds_alternative<uint64>(pair.second)) {
                valueStr = fmt::format("uint64: {}", std::get<uint64>(pair.second));
            } else if (std::holds_alternative<double>(pair.second)) {
                valueStr = fmt::format("double: {}", std::get<double>(pair.second));
            } else if (std::holds_alternative<std::string>(pair.second)) {
                valueStr = fmt::format("string: '{}'", std::get<std::string>(pair.second));
            }
            logger.debug("  Key: '{}', Value: {}", pair.first, valueStr);
        }

        if (backButton) {
            player.sendMessage("§a您取消了转账操作。");
            return;
        }

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

        if (confirmTransfer) {
            if (newSelectedCurrency.empty()) {
                player.sendMessage("§c请选择一个经济类型。");
                showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }

            std::optional<std::string> selectedPlayerUuid;
            std::optional<std::string> selectedPlayerName;
            int selectedCount = 0;

            int startIndex = newPage * PLAYERS_PER_PAGE;
            int endIndex = std::min(startIndex + PLAYERS_PER_PAGE, (int)tempFilteredPlayers.size());

            for (int i = startIndex; i < endIndex; ++i) {
                const auto& playerEntry = tempFilteredPlayers[i];
                std::string playerUuidStr = playerEntry.uuid.asString();
                auto toggleResult = getFormValue(playerUuidStr);
                if (std::holds_alternative<uint64>(toggleResult) && static_cast<bool>(std::get<uint64>(toggleResult))) {
                    selectedPlayerUuid = playerUuidStr;
                    selectedPlayerName = playerEntry.name;
                    selectedCount++;
                }
            }

            if (selectedCount == 0) {
                player.sendMessage("§c请选择一个目标玩家。");
                showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }
            if (selectedCount > 1) {
                player.sendMessage("§c一次只能选择一个目标玩家进行转账。");
                showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }

            std::string targetPlayerUuid = selectedPlayerUuid.value();
            std::string targetPlayerName = selectedPlayerName.value();

            // 检查是否是自己给自己转账
            if (playerUuid == targetPlayerUuid) {
                player.sendMessage("§c您不能自己给自己转账！");
                showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }

            std::optional<int64_t> rawAmountOpt = czmoney::api::parseBalance(amountStr);
            if (!rawAmountOpt.has_value() || rawAmountOpt.value() <= 0) {
                player.sendMessage("§c请输入有效的正数金额。");
                showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }
            double amountToTransfer = static_cast<double>(rawAmountOpt.value()) / 100.0;

            // 检查所选货币类型是否允许转账
            const auto& currentConfig = czmoney::MyMod::getInstance().getConfig();
            auto currencyConfigIt = currentConfig.economy.find(newSelectedCurrency);
            if (currencyConfigIt == currentConfig.economy.end() || !currencyConfigIt->second.allowTransfer) {
                player.sendMessage(fmt::format("§c经济类型 '{}' 不允许转账。", newSelectedCurrency));
                showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
                return;
            }

            logger.debug("尝试转账：从 {} 到 {}，金额 {} {}。", player.getRealName(), targetPlayerName, amountToTransfer, newSelectedCurrency);
            czmoney::api::MoneyApiResult transferResult = czmoney::api::transferBalance( // 调用 API
                player.getUuid().asString(),
                targetPlayerUuid,
                newSelectedCurrency, // 使用用户选择的货币类型
                amountToTransfer,
                "Transfer",
                player.getRealName(),
                targetPlayerName
            );
            logger.debug("转账结果：{}", transferResult == czmoney::api::MoneyApiResult::Success ? "成功" : "失败");

            if (transferResult == czmoney::api::MoneyApiResult::Success) {
                player.sendMessage(fmt::format("§a成功向 {} 转账 {} {}。",
                                               targetPlayerName,
                                               czmoney::api::formatBalance(rawAmountOpt.value()),
                                               newSelectedCurrency));
            } else {
                std::string errorMessage;
                switch (transferResult) {
                    case czmoney::api::MoneyApiResult::InvalidAmount:
                        errorMessage = "无效金额。";
                        break;
                    case czmoney::api::MoneyApiResult::InsufficientBalance:
                        errorMessage = "余额不足。";
                        break;
                    case czmoney::api::MoneyApiResult::DatabaseError:
                        errorMessage = "数据库操作失败。";
                        break;
                    case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                        errorMessage = "经济系统不可用。";
                        break;
                    case czmoney::api::MoneyApiResult::AccountNotFound:
                        errorMessage = "目标玩家账户不存在。"; // 更明确的提示
                        break;
                    case czmoney::api::MoneyApiResult::UnknownError:
                    default:
                        errorMessage = "未知错误。";
                        break;
                }
                player.sendMessage(fmt::format("§c转账失败！{}. 请检查您的余额或目标玩家账户。", errorMessage));
            }
            return;
        }

        // 如果搜索过滤器、页码或货币类型发生变化，则重新显示表单
        if (newSearchFilter != initialSearchFilter || newPage != initialPage || newSelectedCurrency != initialSelectedCurrency) {
            showTransferForm(player, newSearchFilter, newPage, newSelectedCurrency);
            return;
        }

        player.sendMessage("§e请勾选 '确认转账' 以继续。");
    });
}

} // namespace czmoney::ui
