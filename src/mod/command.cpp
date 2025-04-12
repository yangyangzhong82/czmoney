#include "mod/command.h"
#include "mod/MyMod.h" // 获取 MoneyManager 和 Config
#include "mod/money.h" // 使用 MoneyManager 功能
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "mc/server/commands/CommandOutput.h" // Corrected include path
#include "mc/server/commands/CommandOrigin.h" // Corrected include path
#include "mc/server/commands/Command.h"       // Added include for Command
// #include "ll/api/command/CommandUtils.h" // Removed CommandUtils include
// #include "ll/api/i18n/I18n.h" // Removed i18n include
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/platform/UUID.h" // Player::getUuid() 返回 mce::UUID
#include <string>
#include <vector>
#include <optional>
#include <cmath>        // For std::round, std::isnan, std::isinf
#include <limits>       // For std::numeric_limits
#include <fmt/format.h> // 包含 fmt 用于格式化字符串

namespace my_mod {

using ll::command::CommandRegistrar;
using ll::command::CommandHandle;
// using ll::command::CommandOutput; // Removed incorrect using
// using ll::command::CommandOrigin; // Removed incorrect using
// using ll::command::Command; // Removed incorrect using
// using ll::i18n_literals::operator""_tr; // Removed i18n using

// 辅助函数：获取要操作的货币类型
std::string getTargetCurrencyType(const std::string& inputType) {
    if (!inputType.empty()) {
        return inputType; // 如果用户指定了类型，则使用用户指定的
    }
    // TODO: 从配置中获取默认货币类型
    // return MyMod::getInstance().getConfig().defaultCurrencyType;
    return "money"; // 暂时硬编码为 "money"
}

// 辅助函数：向命令输出发送反馈
void sendFeedback(CommandOutput& output, const std::string& message, bool isSuccess) {
    if (isSuccess) {
        output.success(message);
    } else {
        output.error(message);
    }
}


void registerMoneyCommands() {
    auto& registrar = CommandRegistrar::getInstance();
    auto& moneyCommand = registrar.getOrCreateCommand(
        "cmoney",
        "Manage player balances", // 命令描述 (removed _tr)
        CommandPermissionLevel::Any    // 默认权限级别 (子命令可以覆盖)
    );

    // --- 注册带选择器的子命令 ---

    // 1. money query <target> [currencyType] - 查询玩家余额
    moneyCommand.overload<MoneyQuerySelectorArgs>()
        .text("query") // 子命令名称
        .required("target") // 目标选择器参数
        .optional("currencyType") // 可选的货币类型参数
        // .permission(CommandPermissionLevel::GameDirectors) // Removed permission call
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyQuerySelectorArgs const& args, ::Command const&) { // Corrected Command namespace
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                auto results = args.target.results(origin); // 解析目标选择器

                if (results.empty()) {
                    output.error("No matching players found."); // removed _tr
                    return;
                }

                for (Player* player : results) {
                    if (!player) continue; // 跳过无效的玩家指针

                    std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                    int64_t balance = moneyManager.getPlayerBalanceOrInit(uuidStr, currency); // 获取或初始化余额

                    // 构建反馈消息
                    std::string feedback = fmt::format(
                        "{}'s balance ({}): {}", // removed _tr().toString()
                        player->getRealName(),
                        currency,
                        MoneyManager::formatBalance(balance)
                    );
                    sendFeedback(output, feedback, true); // 发送成功反馈
                }
            }
        );

    // 2. money set <target> <amount> [currencyType] - 设置玩家余额
    moneyCommand.overload<MoneySetSelectorArgs>()
        .text("set")
        .required("target")
        .required("amount") // Changed from amountStr
        .optional("currencyType")
        // .permission(CommandPermissionLevel::GameDirectors) // Removed permission call
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneySetSelectorArgs const& args, ::Command const&) { // Corrected Command namespace
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // Validate input amount (NaN, Infinity, negative)
                if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                }
                if (inputAmount < 0.0f) {
                     output.error("Amount cannot be negative for set operation."); // removed _tr
                     return;
                }

                // Convert to internal representation (cents), truncate, and check for overflow
                float centsFloat = inputAmount * 100.0f; // Calculate cents as float
                // Check for potential overflow before casting to int64_t
                if (centsFloat < static_cast<float>(std::numeric_limits<int64_t>::min()) ||
                    centsFloat >= static_cast<float>(std::numeric_limits<int64_t>::max()) + 1.0f) { // Check against max+1 due to truncation
                    output.error(fmt::format("Amount {} is too large or small and would cause an overflow after conversion.", inputAmount));
                    return;
                }
                // Cast to int64_t performs truncation towards zero
                int64_t amountInCents = static_cast<int64_t>(centsFloat);


                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("No matching players found."); // removed _tr
                    return;
                }

                int successCount = 0;
                int failCount = 0;
                for (Player* player : results) {
                    if (!player) {
                        failCount++;
                        continue;
                    }
                    std::string uuidStr = player->getUuid().asString();
                    if (moneyManager.setPlayerBalance(uuidStr, currency, amountInCents)) { // Use amountInCents
                        successCount++;
                        // 可以选择性地给每个玩家发送单独的成功消息，或者最后统一发送
                        // sendFeedback(output, fmt::format("Set {}'s {} balance to {}", player->getRealName(), currency, MoneyManager::formatBalance(amountInCents)), true);
                    } else {
                        failCount++;
                        // 记录具体哪个玩家失败
                         output.error(fmt::format("Failed to set balance for {}.", player->getRealName())); // removed _tr().toString()
                    }
                }
                 // 发送总结反馈
                 sendFeedback(output, fmt::format("Set balance for {} players, {} failed.", successCount, failCount), successCount > 0); // removed _tr().toString()
            }
        );

    // 3. money add <target> <amount> [currencyType] - 增加玩家余额
    moneyCommand.overload<MoneyAddSelectorArgs>()
        .text("add")
        .required("target")
        .required("amount") // Changed from amountStr
        .optional("currencyType")
        // .permission(CommandPermissionLevel::GameDirectors) // Removed permission call
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyAddSelectorArgs const& args, ::Command const&) { // Corrected Command namespace
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // Validate input amount (NaN, Infinity, non-positive)
                 if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                 }
                 if (inputAmount <= 0.0f) {
                     output.error("Amount to add must be positive."); // removed _tr
                     return;
                 }

                // Convert to internal representation (cents), truncate, and check for overflow
                float centsFloat = inputAmount * 100.0f; // Calculate cents as float
                // Since inputAmount > 0, only check against max before casting
                if (centsFloat >= static_cast<float>(std::numeric_limits<int64_t>::max()) + 1.0f) { // Check against max+1 due to truncation
                    output.error(fmt::format("Amount {} is too large and would cause an overflow after conversion.", inputAmount));
                    return;
                }
                 // Cast to int64_t performs truncation towards zero
                int64_t amountToAddInCents = static_cast<int64_t>(centsFloat);


                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("No matching players found."); // removed _tr
                    return;
                }

                int successCount = 0;
                int failCount = 0;
                for (Player* player : results) {
                    if (!player) {
                        failCount++;
                        continue;
                    }
                    std::string uuidStr = player->getUuid().asString();
                    if (moneyManager.addPlayerBalance(uuidStr, currency, amountToAddInCents)) { // Use amountToAddInCents
                        successCount++;
                        // 可选：单独反馈
                        // sendFeedback(output, fmt::format("Added {} {} to {}", MoneyManager::formatBalance(amountToAddInCents), currency, player->getRealName()), true);
                    } else {
                        failCount++;
                        output.error(fmt::format("Failed to add balance for {} (maybe overflow?).", player->getRealName())); // removed _tr().toString()
                    }
                }
                sendFeedback(output, fmt::format("Added balance for {} players, {} failed.", successCount, failCount), successCount > 0); // removed _tr().toString()
            }
        );

    // 4. money reduce <target> <amount> [currencyType] - 减少玩家余额
    moneyCommand.overload<MoneyReduceSelectorArgs>()
        .text("reduce")
        .required("target")
        .required("amount") // Changed from amountStr
        .optional("currencyType")
        // .permission(CommandPermissionLevel::GameDirectors) // Removed permission call
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyReduceSelectorArgs const& args, ::Command const&) { // Corrected Command namespace
                 // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
               auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // Validate input amount (NaN, Infinity, non-positive)
                 if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                 }
                 if (inputAmount <= 0.0f) {
                     output.error("Amount to reduce must be positive."); // removed _tr
                     return;
                 }

                 // Convert to internal representation (cents), truncate, and check for overflow
                float centsFloat = inputAmount * 100.0f; // Calculate cents as float
                 // Since inputAmount > 0, only check against max before casting
                if (centsFloat >= static_cast<float>(std::numeric_limits<int64_t>::max()) + 1.0f) { // Check against max+1 due to truncation
                    output.error(fmt::format("Amount {} is too large and would cause an overflow after conversion.", inputAmount));
                    return;
                }
                 // Cast to int64_t performs truncation towards zero
                int64_t amountToReduceInCents = static_cast<int64_t>(centsFloat);


                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("No matching players found."); // removed _tr
                    return;
                }

                int successCount = 0;
                int failCount = 0;
                for (Player* player : results) {
                    if (!player) {
                        failCount++;
                        continue;
                    }
                    std::string uuidStr = player->getUuid().asString();
                    // 注意：subtractPlayerBalance 不会自动初始化账户
                    if (moneyManager.subtractPlayerBalance(uuidStr, currency, amountToReduceInCents)) { // Use amountToReduceInCents
                        successCount++;
                        // 可选：单独反馈
                        // sendFeedback(output, fmt::format("Reduced {} {} from {}", MoneyManager::formatBalance(amountToReduceInCents), currency, player->getRealName()), true);
                    } else {
                        failCount++;
                        // 获取当前余额以提供更详细的错误信息
                        std::optional<int64_t> currentBalanceOpt = moneyManager.getPlayerBalance(uuidStr, currency);
                        if (!currentBalanceOpt.has_value()) {
                             output.error(fmt::format("Failed to reduce balance for {}: Account does not exist.", player->getRealName())); // removed _tr().toString()
                        } else if (currentBalanceOpt.value() < amountToReduceInCents) { // Compare with cents
                             output.error(fmt::format("Failed to reduce balance for {}: Insufficient funds (has {}).", player->getRealName(), MoneyManager::formatBalance(currentBalanceOpt.value()))); // removed _tr().toString()
                        } else {
                             output.error(fmt::format("Failed to reduce balance for {} (unknown reason).", player->getRealName())); // removed _tr().toString()
                        }
                    }
                }
                sendFeedback(output, fmt::format("Reduced balance for {} players, {} failed.", successCount, failCount), successCount > 0); // removed _tr().toString()
            }
        );

    // TODO: 添加其他命令重载，例如：
    // - money query [currencyType] (查询自身余额)
    // - money pay <player> <amount> [currencyType] (转账)
    // - money top [count] [currencyType] (排行榜)
    // - 不带选择器的管理员命令 (money set <playerName> ...)

}

} // namespace my_mod
