#include "czmoney/command.h"
#include "czmoney/MyMod.h" // 获取 MoneyManager 和 Config
#include "czmoney/money.h" // 使用 MoneyManager 功能
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/EnumName.h" // 新增: 用于获取 SoftEnum 名称
#include "ll/api/command/SoftEnum.h" // 新增: 明确包含 SoftEnum
#include "czmoney/config.h" // 确保包含 Config
#include "mc/server/commands/CommandOutput.h" // Corrected include path
#include "mc/server/commands/CommandOrigin.h" // Corrected include path
#include "mc/server/commands/Command.h"       // Added include for Command
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/platform/UUID.h" // Player::getUuid() 返回 mce::UUID
#include "ll/api/service/PlayerInfo.h" // 新增: 包含 PlayerInfo 服务
#include <string>
#include <vector>
#include <optional>
#include <cmath>        // For std::round, std::isnan, std::isinf
#include <limits>       // For std::numeric_limits
#include <fmt/format.h> // 包含 fmt 用于格式化字符串
#include <vector>       // 用于存储从配置中提取的货币类型

namespace czmoney {

using ll::command::CommandRegistrar;
using ll::command::CommandHandle;
using ll::command::SoftEnum; // 新增 using
using ll::service::PlayerInfo; // 新增 using

// 辅助函数：获取要操作的货币类型
// 修改参数类型为 SoftEnum
std::string getTargetCurrencyType(const SoftEnum<CurrencyTypeEnum>& inputType) {
    // SoftEnum 隐式转换为 std::string，可以直接检查是否为空
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
    auto& logger = MyMod::getInstance().getSelf().getLogger(); // 获取 logger

    // --- 注册/更新 SoftEnum ---
    const auto& config = MyMod::getInstance().getConfig(); // 获取配置
    std::vector<std::string> currencyTypes; // 存储从配置中读取的货币类型
    for (const auto& pair : config.economy) {
        currencyTypes.push_back(pair.first); // 提取 map 的键 (货币类型名称)
    }

    // 获取 SoftEnum 的注册名称
    const std::string currencyEnumName = ll::command::enum_name_v<SoftEnum<CurrencyTypeEnum>>;

    // 尝试注册 SoftEnum，如果已存在则更新其值
    if (!registrar.hasSoftEnum(currencyEnumName)) {
        logger.info("Registering SoftEnum '{}' for currency types.", currencyEnumName);
        if (!registrar.tryRegisterSoftEnum(currencyEnumName, currencyTypes)) {
            logger.error("Failed to register SoftEnum '{}'.", currencyEnumName);
            // 可以选择在这里返回或抛出异常，如果 SoftEnum 注册失败是关键错误
        }
    } else {
        logger.info("Updating SoftEnum '{}' with currency types from config.", currencyEnumName);
        if (!registrar.setSoftEnumValues(currencyEnumName, currencyTypes)) {
             logger.error("Failed to update SoftEnum '{}'.", currencyEnumName);
        }
    }
    // --- SoftEnum 注册/更新结束 ---


    auto& moneyCommand = registrar.getOrCreateCommand(
        "cmoney",
        "Manage player balances", // 命令描述
        CommandPermissionLevel::Any    // 默认权限级别
    );

    // --- 注册带选择器的子命令 ---

    // 1. money query <target> [currencyType] - 查询玩家余额
    moneyCommand.overload<MoneyQuerySelectorArgs>()
        .text("query") // 子命令名称
        .required("target") // 目标选择器参数
        .optional("currencyType") // 可选的货币类型参数 (现在是 SoftEnum)
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyQuerySelectorArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                // 使用修改后的 getTargetCurrencyType
                std::string currency = getTargetCurrencyType(args.currencyType);
                auto results = args.target.results(origin); // 解析目标选择器

                if (results.empty()) {
                    output.error("No matching players found.");
                    return;
                }

                for (Player* player : results) {
                    if (!player) continue; // 跳过无效的玩家指针

                    std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                    int64_t balance = moneyManager.getPlayerBalanceOrInit(uuidStr, currency); // 获取或初始化余额

                    // 构建反馈消息
                    std::string feedback = fmt::format(
                        "{}'s balance ({}): {}",
                        player->getRealName(),
                        currency,
                        MoneyManager::formatBalance(balance)
                    );
                    sendFeedback(output, feedback, true); // 发送成功反馈
                }
            }
        );

    // 新增: 1.1 money query <playerName> [currencyType] - 查询离线玩家余额
    moneyCommand.overload<MoneyQueryOfflineArgs>()
        .text("query")
        .required("playerName")
        .optional("currencyType")
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyQueryOfflineArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);

                // 使用 PlayerInfo 获取玩家信息
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString(); // 获取 UUID

                int64_t balance = moneyManager.getPlayerBalanceOrInit(uuidStr, currency); // 获取或初始化余额

                // 构建反馈消息
                std::string feedback = fmt::format(
                    "{}'s balance ({}): {}",
                    playerInfo.name, // 使用 PlayerInfo 中的名字
                    currency,
                    MoneyManager::formatBalance(balance)
                );
                sendFeedback(output, feedback, true); // 发送成功反馈
            }
        );


    // 2. money set <target> <amount> [currencyType] - 设置在线玩家余额
    moneyCommand.overload<MoneySetSelectorArgs>()
        .text("set")
        .required("target")
        .required("amount")
        .optional("currencyType") // 可选的货币类型参数 (现在是 SoftEnum)
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneySetSelectorArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                // 使用修改后的 getTargetCurrencyType
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // Validate input amount (NaN, Infinity, negative)
                if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                }
                if (inputAmount < 0.0f) {
                     output.error("Amount cannot be negative for set operation.");
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
                    output.error("No matching players found.");
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
                    } else {
                        failCount++;
                         output.error(fmt::format("Failed to set balance for {}.", player->getRealName()));
                    }
                }
                 sendFeedback(output, fmt::format("Set balance for {} players, {} failed.", successCount, failCount), successCount > 0);
            }
        );

    // 新增: 2.1 money set <playerName> <amount> [currencyType] - 设置离线玩家余额
    moneyCommand.overload<MoneySetOfflineArgs>()
        .text("set")
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneySetOfflineArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount;

                // Validate input amount
                if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                }
                if (inputAmount < 0.0f) {
                     output.error("Amount cannot be negative for set operation.");
                     return;
                }

                // Convert to internal representation
                float centsFloat = inputAmount * 100.0f;
                if (centsFloat < static_cast<float>(std::numeric_limits<int64_t>::min()) ||
                    centsFloat >= static_cast<float>(std::numeric_limits<int64_t>::max()) + 1.0f) {
                    output.error(fmt::format("Amount {} is too large or small and would cause an overflow after conversion.", inputAmount));
                    return;
                }
                int64_t amountInCents = static_cast<int64_t>(centsFloat);

                // Get player info
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString();

                // Set balance
                if (moneyManager.setPlayerBalance(uuidStr, currency, amountInCents)) {
                    sendFeedback(output, fmt::format("Set {}'s balance ({}) to {}.", playerInfo.name, currency, MoneyManager::formatBalance(amountInCents)), true);
                } else {
                    sendFeedback(output, fmt::format("Failed to set balance for {}.", playerInfo.name), false);
                }
            }
        );

    // 3. money add <target> <amount> [currencyType] - 增加在线玩家余额
    moneyCommand.overload<MoneyAddSelectorArgs>()
        .text("add")
        .required("target")
        .required("amount")
        .optional("currencyType") // 可选的货币类型参数 (现在是 SoftEnum)
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyAddSelectorArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                // 使用修改后的 getTargetCurrencyType
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // Validate input amount (NaN, Infinity, non-positive)
                 if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                 }
                 if (inputAmount <= 0.0f) {
                     output.error("Amount to add must be positive.");
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
                    output.error("No matching players found.");
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
                    } else {
                        failCount++;
                        output.error(fmt::format("Failed to add balance for {} (maybe overflow?).", player->getRealName()));
                    }
                }
                sendFeedback(output, fmt::format("Added balance for {} players, {} failed.", successCount, failCount), successCount > 0);
            }
        );

    // 新增: 3.1 money add <playerName> <amount> [currencyType] - 增加离线玩家余额
    moneyCommand.overload<MoneyAddOfflineArgs>()
        .text("add")
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyAddOfflineArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount;

                // Validate input amount
                if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                }
                if (inputAmount <= 0.0f) {
                    output.error("Amount to add must be positive.");
                    return;
                }

                // Convert to internal representation
                float centsFloat = inputAmount * 100.0f;
                if (centsFloat >= static_cast<float>(std::numeric_limits<int64_t>::max()) + 1.0f) {
                    output.error(fmt::format("Amount {} is too large and would cause an overflow after conversion.", inputAmount));
                    return;
                }
                int64_t amountToAddInCents = static_cast<int64_t>(centsFloat);

                // Get player info
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString();

                // Add balance
                if (moneyManager.addPlayerBalance(uuidStr, currency, amountToAddInCents)) {
                    // Get the new balance to display it
                    int64_t newBalance = moneyManager.getPlayerBalanceOrInit(uuidStr, currency); // Re-fetch or assume success
                    sendFeedback(output, fmt::format("Added {} to {}'s balance ({}). New balance: {}", MoneyManager::formatBalance(amountToAddInCents), playerInfo.name, currency, MoneyManager::formatBalance(newBalance)), true);
                } else {
                    sendFeedback(output, fmt::format("Failed to add balance for {} (maybe overflow?).", playerInfo.name), false);
                }
            }
        );

    // 4. money reduce <target> <amount> [currencyType] - 减少在线玩家余额
    moneyCommand.overload<MoneyReduceSelectorArgs>()
        .text("reduce")
        .required("target")
        .required("amount")
        .optional("currencyType") // 可选的货币类型参数 (现在是 SoftEnum)
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyReduceSelectorArgs const& args, ::Command const&) {
                 // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
               auto& moneyManager = MyMod::getInstance().getMoneyManager();
                // 使用修改后的 getTargetCurrencyType
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // Validate input amount (NaN, Infinity, non-positive)
                 if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                 }
                 if (inputAmount <= 0.0f) {
                     output.error("Amount to reduce must be positive.");
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
                    output.error("No matching players found.");
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
                    } else {
                        failCount++;
                        // 获取当前余额以提供更详细的错误信息
                        std::optional<int64_t> currentBalanceOpt = moneyManager.getPlayerBalance(uuidStr, currency);
                        if (!currentBalanceOpt.has_value()) {
                             output.error(fmt::format("Failed to reduce balance for {}: Account does not exist.", player->getRealName()));
                        } else if (currentBalanceOpt.value() < amountToReduceInCents) { // Compare with cents
                             output.error(fmt::format("Failed to reduce balance for {}: Insufficient funds (has {}).", player->getRealName(), MoneyManager::formatBalance(currentBalanceOpt.value())));
                        } else {
                             output.error(fmt::format("Failed to reduce balance for {} (unknown reason).", player->getRealName()));
                        }
                    }
                }
                sendFeedback(output, fmt::format("Reduced balance for {} players, {} failed.", successCount, failCount), successCount > 0);
            }
        );


    // 新增: 4.1 money reduce <playerName> <amount> [currencyType] - 减少离线玩家余额
    moneyCommand.overload<MoneyReduceOfflineArgs>()
        .text("reduce")
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyReduceOfflineArgs const& args, ::Command const&) {
                // --- Permission Check ---
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
                // --- End Permission Check ---
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount;

                // Validate input amount
                if (std::isnan(inputAmount) || std::isinf(inputAmount)) {
                    output.error("Invalid amount provided (NaN or Infinity).");
                    return;
                }
                if (inputAmount <= 0.0f) {
                    output.error("Amount to reduce must be positive.");
                    return;
                }

                // Convert to internal representation
                float centsFloat = inputAmount * 100.0f;
                if (centsFloat >= static_cast<float>(std::numeric_limits<int64_t>::max()) + 1.0f) {
                    output.error(fmt::format("Amount {} is too large and would cause an overflow after conversion.", inputAmount));
                    return;
                }
                int64_t amountToReduceInCents = static_cast<int64_t>(centsFloat);

                // Get player info
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString();

                // Subtract balance (does not init account)
                std::optional<int64_t> currentBalanceOpt = moneyManager.getPlayerBalance(uuidStr, currency);
                if (!currentBalanceOpt.has_value()) {
                    sendFeedback(output, fmt::format("Failed to reduce balance for {}: Account does not exist.", playerInfo.name), false);
                    return;
                }
                if (currentBalanceOpt.value() < amountToReduceInCents) {
                    sendFeedback(output, fmt::format("Failed to reduce balance for {}: Insufficient funds (has {}).", playerInfo.name, MoneyManager::formatBalance(currentBalanceOpt.value())), false);
                    return;
                }

                if (moneyManager.subtractPlayerBalance(uuidStr, currency, amountToReduceInCents)) {
                    int64_t newBalance = moneyManager.getPlayerBalanceOrInit(uuidStr, currency); // Re-fetch
                    sendFeedback(output, fmt::format("Reduced {} from {}'s balance ({}). New balance: {}", MoneyManager::formatBalance(amountToReduceInCents), playerInfo.name, currency, MoneyManager::formatBalance(newBalance)), true);
                } else {
                    // Handle potential errors from subtractPlayerBalance itself (e.g., overflow check inside)
                    sendFeedback(output, fmt::format("Failed to reduce balance for {} (unknown reason).", playerInfo.name), false);
                }
            }
        );


    // TODO: 添加其他命令重载，例如：
    // - money query [currencyType] (查询自身余额)
    // - money pay <player> <amount> [currencyType] (转账)
    // - money top [count] [currencyType] (排行榜)
    // - 不带选择器的管理员命令 (money set <playerName> ...) // 已部分实现

} // registerMoneyCommands function end

} // namespace czmoney
