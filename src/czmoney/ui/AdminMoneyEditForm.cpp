#include "czmoney/ui/AdminMoneyEditForm.h"
#include "czmoney/MyMod.h"
#include "czmoney/logger.h"
#include "czmoney/money/money_api.h"
#include "czmoney/ui/AdminMoneyListForm.h" // 引入 AdminMoneyListForm
#include "ll/api/form/ModalForm.h"
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

std::string AdminMoneyEditForm::getPlayerName(const std::string& uuid) {
    auto playerInfoOpt = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuid));
    if (playerInfoOpt.has_value()) {
        return playerInfoOpt.value().name;
    }
    logger.warn("无法获取 UUID {} 的玩家名称，将显示为 '未知玩家'。", uuid);
    return "未知玩家";
}

AdminMoneyEditForm::AdminMoneyEditForm(
    Player& player,
    const std::string& targetPlayerUuid,
    const std::string& initialCurrency,
    const std::string& returnSearchFilter,
    int returnPage
)
    : ll::form::CustomForm("经济管理 - 玩家编辑"),
      mPlayer(player),
      mTargetPlayerUuid(targetPlayerUuid),
      mTargetPlayerName(getPlayerName(targetPlayerUuid)),
      mSelectedCurrency(initialCurrency),
      mReturnSearchFilter(returnSearchFilter),
      mReturnPage(returnPage)
{
    // 更新表单标题以包含玩家名称
    setTitle(fmt::format("经济管理 - {}", mTargetPlayerName));

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

    // 1. 显示目标玩家信息
    appendLabel(fmt::format("§l目标玩家: §r{} (§7{})", mTargetPlayerName, mTargetPlayerUuid));

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

    // 3. 显示当前余额
    std::optional<double> currentBalanceOpt = czmoney::api::getPlayerBalance(mTargetPlayerUuid, mSelectedCurrency);
    std::string currentBalanceStr = currentBalanceOpt.has_value() ? czmoney::api::formatBalance(static_cast<int64_t>(currentBalanceOpt.value() * 100)) : "N/A";
    appendLabel(fmt::format("§l当前余额: §r{} {}", currentBalanceStr, mSelectedCurrency));

    // 4. 金额输入框
    appendInput("amount_input", "金额", "输入金额 (例如: 100.50)", "");

    // 5. 操作类型选择
    appendDropdown("action_type", "选择操作类型", {"设置 (Set)", "增加 (Add)", "减少 (Subtract)"}, 0);

    // 6. 确认按钮
    appendToggle("confirm_action", "确认操作", false);

    // 7. 返回按钮
    appendToggle("back_button", "返回列表", false);
}

void showAdminMoneyEditForm(
    Player& player,
    const std::string& targetPlayerUuid,
    const std::string& initialCurrency,
    const std::string& returnSearchFilter,
    int returnPage
) {
    using ll::form::CustomFormResult;
    using ll::form::FormCancelReason;
    using ll::form::CustomFormElementResult;

    auto form = std::make_unique<AdminMoneyEditForm>(player, targetPlayerUuid, initialCurrency, returnSearchFilter, returnPage);
    std::string targetPlayerName = form->getTargetPlayerName(); // 获取目标玩家名称

    // 获取所有可用的货币类型 (用于回调中重新构建表单)
    std::vector<std::string> availableCurrenciesForCallback;
    const auto& config = czmoney::MyMod::getInstance().getConfig();
    for (const auto& pair : config.economy) {
        availableCurrenciesForCallback.push_back(pair.first);
    }

    form->sendTo(player, [
        playerUuid = player.getUuid().asString(),
        targetPlayerUuid,
        targetPlayerName,
        initialCurrency,
        returnSearchFilter,
        returnPage,
        availableCurrenciesForCallback
    ](Player& player, CustomFormResult const& data, FormCancelReason reason) {
        if (!data.has_value()) {
            logger.debug("经济管理编辑表单被玩家 {} 关闭。", player.getRealName());
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

        std::string newSelectedCurrency = initialCurrency;
        auto currencyTypeResult = getFormValue("currency_type");
        if (std::holds_alternative<std::string>(currencyTypeResult)) {
            newSelectedCurrency = std::get<std::string>(currencyTypeResult);
        } else if (std::holds_alternative<uint64>(currencyTypeResult)) {
            size_t selectedIndex = static_cast<size_t>(std::get<uint64>(currencyTypeResult));
            if (selectedIndex < availableCurrenciesForCallback.size()) {
                newSelectedCurrency = availableCurrenciesForCallback[selectedIndex];
            }
        }

        std::string amountStr = "";
        auto amountStrResult = getFormValue("amount_input");
        if (std::holds_alternative<std::string>(amountStrResult)) {
            amountStr = std::get<std::string>(amountStrResult);
        }

        int actionType = 0; // 0: Set, 1: Add, 2: Subtract
        auto actionTypeResult = getFormValue("action_type");
        if (std::holds_alternative<uint64>(actionTypeResult)) {
            actionType = static_cast<int>(std::get<uint64>(actionTypeResult));
        }

        bool confirmAction = false;
        auto confirmActionResult = getFormValue("confirm_action");
        if (std::holds_alternative<uint64>(confirmActionResult)) {
            confirmAction = static_cast<bool>(std::get<uint64>(confirmActionResult));
        }

        bool backButton = false;
        auto backButtonResult = getFormValue("back_button");
        if (std::holds_alternative<uint64>(backButtonResult)) {
            backButton = static_cast<bool>(std::get<uint64>(backButtonResult));
        }

        // 检查是否点击了返回按钮
        if (backButton) {
            showAdminMoneyListForm(player, returnSearchFilter, returnPage, newSelectedCurrency);
            return;
        }

        // 如果货币类型发生变化，重新显示表单以更新余额显示
        if (newSelectedCurrency != initialCurrency) {
            showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
            return;
        }

        if (confirmAction) {
            std::optional<int64_t> rawAmountOpt = czmoney::api::parseBalance(amountStr);
            if (!rawAmountOpt.has_value()) {
                player.sendMessage("§c请输入有效的金额。");
                showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
                return;
            }
            double amount = static_cast<double>(rawAmountOpt.value()) / 100.0;

            czmoney::api::MoneyApiResult result = czmoney::api::MoneyApiResult::UnknownError;
            std::string reason1 = "AdminUI";
            std::string reason2 = fmt::format("Admin: {}", player.getRealName());
            std::string reason3 = fmt::format("Target: {}", targetPlayerName);

            switch (actionType) {
                case 0: // Set
                    result = czmoney::api::setPlayerBalance(targetPlayerUuid, newSelectedCurrency, amount, reason1, reason2, reason3);
                    break;
                case 1: // Add
                    if (amount <= 0) {
                        player.sendMessage("§c增加金额必须为正数。");
                        showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
                        return;
                    }
                    result = czmoney::api::addPlayerBalance(targetPlayerUuid, newSelectedCurrency, amount, reason1, reason2, reason3);
                    break;
                case 2: // Subtract
                    if (amount <= 0) {
                        player.sendMessage("§c减少金额必须为正数。");
                        showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
                        return;
                    }
                    result = czmoney::api::subtractPlayerBalance(targetPlayerUuid, newSelectedCurrency, amount, reason1, reason2, reason3);
                    break;
                default:
                    player.sendMessage("§c无效的操作类型。");
                    showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
                    return;
            }

            if (result == czmoney::api::MoneyApiResult::Success) {
                player.sendMessage(fmt::format("§a成功对玩家 {} 的 {} 余额执行操作。", targetPlayerName, newSelectedCurrency));
            } else {
                std::string errorMessage;
                switch (result) {
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
                        errorMessage = "目标玩家账户不存在。";
                        break;
                    case czmoney::api::MoneyApiResult::UnknownError:
                    default:
                        errorMessage = "未知错误。";
                        break;
                }
                player.sendMessage(fmt::format("§c操作失败！{}. 请检查日志。", errorMessage));
            }
            // 操作完成后，重新显示表单以更新余额
            showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
            return;
        }

        // 如果没有任何操作，重新显示表单 (例如，只是点击了空白区域)
        showAdminMoneyEditForm(player, targetPlayerUuid, newSelectedCurrency, returnSearchFilter, returnPage);
    });
}

} // namespace czmoney::ui
