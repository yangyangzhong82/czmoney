#include "czmoney/command.h"
#include "czmoney/MyMod.h" 
#include "czmoney/money.h" 
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/EnumName.h" 
#include "ll/api/command/SoftEnum.h" 
#include "czmoney/config.h" 
#include "mc/server/commands/CommandOutput.h" 
#include "mc/server/commands/CommandOrigin.h" 
#include "mc/server/commands/Command.h"       
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/platform/UUID.h" 
#include "ll/api/service/PlayerInfo.h" 
#include <string>
#include <vector>
#include <optional>
#include <cmath>        
#include <limits>      
#include <fmt/format.h> 
#include <vector>       

namespace czmoney {

using ll::command::CommandRegistrar;
using ll::command::CommandHandle;
using ll::command::SoftEnum; // 新增 using
using ll::service::PlayerInfo; // 新增 using

// 辅助函数：获取要操作的货币类型
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

// 新增辅助函数：将命令输入的 float 金额转换为 int64_t (分)，并进行校验 (截断)
std::optional<int64_t> convertCommandFloatToInt64(float amount, CommandOutput& output, bool requirePositive = false) {
    // 1. 检查 NaN 和 Infinity
    if (std::isnan(amount) || std::isinf(amount)) {
        output.error(fmt::format("无效的金额输入 (NaN 或 Infinity): {}", amount));
        return std::nullopt;
    }

    // 2. 检查是否要求为正数 (用于 add/reduce)
    if (requirePositive && amount <= 0.0f) {
        // 对于 add/reduce，0 是无效的
        output.error(fmt::format("金额必须为正数，收到: {}", amount));
        return std::nullopt;
    }
     // 对于 set，允许负数
     if (!requirePositive && amount < 0.0f) {
         // 允许负数
     }

    // 3. 转换为分 (不进行四舍五入)
    // 先转 double 提高精度
    double amountDouble = static_cast<double>(amount);
    double centsDouble = amountDouble * 100.0;

    // 4. 检查转换后的值是否在 int64_t 范围内 (截断前的检查)
    const double min_representable = static_cast<double>(std::numeric_limits<int64_t>::min());
    const double max_representable_plus_one = static_cast<double>(std::numeric_limits<int64_t>::max()) + 1.0;

    if (centsDouble < min_representable || centsDouble >= max_representable_plus_one) {
        // 使用更精确的范围提示
        output.error(fmt::format("金额 {} 转换后超出有效范围 [{}, {})",
                                 amount,
                                 MoneyManager::formatBalance(std::numeric_limits<int64_t>::min()), // 格式化最小值
                                 MoneyManager::formatBalance(std::numeric_limits<int64_t>::max()) + ".01" // 格式化最大值+0.01表示上限
                                 ));
        return std::nullopt;
    }

    // 5. 安全地转换为 int64_t (执行截断)
    return static_cast<int64_t>(centsDouble);
}

// 修改函数签名以接受别名列表
void registerMoneyCommands(const std::vector<std::string>& aliases) {
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
        }
    } else {
        logger.info("Updating SoftEnum '{}' with currency types from config.", currencyEnumName);
        if (!registrar.setSoftEnumValues(currencyEnumName, currencyTypes)) {
             logger.error("Failed to update SoftEnum '{}'.", currencyEnumName);
        }
    }
    // --- SoftEnum 注册/更新结束 ---


    auto& moneyCommand = registrar.getOrCreateCommand(
        "czmoney",
        "Manage player balances", // 命令描述
        CommandPermissionLevel::Any,    // 默认权限级别
        CommandFlagValue::NotCheat //无需作弊
    );

    // --- 添加命令别名 (从参数读取) ---
    if (!aliases.empty()) {
        logger.info("Registering command aliases:");
        for (const auto& alias : aliases) {
            if (!alias.empty()) { // 确保别名不为空
                 moneyCommand.alias(alias);
                 logger.info("- {}", alias);
            } else {
                 logger.warn("Skipping empty alias found in config.");
            }
        }
    } else {
        logger.warn("No command aliases found in config or the list is empty.");
    }
    // --- 别名添加结束 ---

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

    //  1.1 money query <playerName> [currencyType] - 查询离线玩家余额
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
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // 使用新的辅助函数转换和验证金额 (允许负数)
                std::optional<int64_t> amountInCentsOpt = convertCommandFloatToInt64(inputAmount, output, false);
                if (!amountInCentsOpt) {
                    return; // 转换或验证失败，错误信息已由辅助函数输出
                }
                int64_t amountInCents = amountInCentsOpt.value();

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
                    // 添加理由
                    std::string reason1 = "Command: cmoney set";
                    if (moneyManager.setPlayerBalance(uuidStr, currency, amountInCents, reason1)) { // Use amountInCents and reason
                        successCount++;
                    } else {
                        failCount++;
                         // 失败原因可能在 MoneyManager 记录，提示用户检查日志或可能的原因
                         output.error(fmt::format("Failed to set balance for {}. Amount might be below minimum allowed. Check logs.", player->getRealName()));
                    }
                }
                 sendFeedback(output, fmt::format("Set balance for {} players, {} failed.", successCount, failCount), successCount > 0);
            }
        );

    //  2.1 money set <playerName> <amount> [currencyType] - 设置离线玩家余额
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

                // 使用新的辅助函数转换和验证金额 (允许负数)
                std::optional<int64_t> amountInCentsOpt = convertCommandFloatToInt64(inputAmount, output, false);
                if (!amountInCentsOpt) {
                    return;
                }
                int64_t amountInCents = amountInCentsOpt.value();

                // Get player info
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString();

                // Set balance with reason
                std::string reason1 = "Command: cmoney set";
                if (moneyManager.setPlayerBalance(uuidStr, currency, amountInCents, reason1)) {
                    sendFeedback(output, fmt::format("Set {}'s balance ({}) to {}.", playerInfo.name, currency, MoneyManager::formatBalance(amountInCents)), true); // Corrected: Use :: for static method
                } else {
                    // 提示可能的原因
                    sendFeedback(output, fmt::format("Failed to set balance for {}. Amount might be below minimum allowed. Check logs.", playerInfo.name), false);
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
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount; // Get the float amount

                // 使用新的辅助函数转换和验证金额 (要求正数)
                std::optional<int64_t> amountToAddInCentsOpt = convertCommandFloatToInt64(inputAmount, output, true);
                 if (!amountToAddInCentsOpt) {
                    return;
                 }
                int64_t amountToAddInCents = amountToAddInCentsOpt.value();

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
                    // 添加理由
                    std::string reason1 = "Command: cmoney add";
                    if (moneyManager.addPlayerBalance(uuidStr, currency, amountToAddInCents, reason1)) { // Use amountToAddInCents and reason
                        successCount++;
                    } else {
                        failCount++;
                        // 增加失败通常是溢出或数据库错误
                        output.error(fmt::format("Failed to add balance for {} (maybe overflow?). Check logs.", player->getRealName()));
                    }
                }
                sendFeedback(output, fmt::format("Added balance for {} players, {} failed.", successCount, failCount), successCount > 0);
            }
        );

    //  3.1 money add <playerName> <amount> [currencyType] - 增加离线玩家余额
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

                // 使用新的辅助函数转换和验证金额 (要求正数)
                std::optional<int64_t> amountToAddInCentsOpt = convertCommandFloatToInt64(inputAmount, output, true);
                if (!amountToAddInCentsOpt) {
                    return;
                }
                int64_t amountToAddInCents = amountToAddInCentsOpt.value();

                // Get player info
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString();

                // Add balance with reason
                std::string reason1 = "Command: cmoney add";
                if (moneyManager.addPlayerBalance(uuidStr, currency, amountToAddInCents, reason1)) {
                    // Get the new balance to display it
                    int64_t newBalance = moneyManager.getPlayerBalanceOrInit(uuidStr, currency); // Re-fetch or assume success
                    sendFeedback(output, fmt::format("Added {} to {}'s balance ({}). New balance: {}", MoneyManager::formatBalance(amountToAddInCents), playerInfo.name, currency, MoneyManager::formatBalance(newBalance)), true); // Corrected: Use :: for static method
                } else {
                    // 增加失败通常是溢出或数据库错误
                    sendFeedback(output, fmt::format("Failed to add balance for {} (maybe overflow?). Check logs.", playerInfo.name), false);
                }
            }
        );

    // 4. money reduce <target> <amount> [currencyType] - 减少在线玩家余额
    moneyCommand.overload<MoneyReduceSelectorArgs>()
        .text("reduce") // 命令文本改为 reduce (或 subtract)
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

                // 使用新的辅助函数转换和验证金额 (要求正数)
                std::optional<int64_t> amountToReduceInCentsOpt = convertCommandFloatToInt64(inputAmount, output, true);
                 if (!amountToReduceInCentsOpt) {
                    return;
                 }
                int64_t amountToReduceInCents = amountToReduceInCentsOpt.value();

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
                    // 添加理由
                    std::string reason1 = "Command: cmoney reduce";
                    // 注意：subtractPlayerBalance 不会自动初始化账户
                    if (moneyManager.subtractPlayerBalance(uuidStr, currency, amountToReduceInCents, reason1)) { // Use amountToReduceInCents and reason
                        successCount++;
                    } else {
                        failCount++;
                        // 获取当前余额以提供更详细的错误信息
                        std::optional<int64_t> currentBalanceOpt = moneyManager.getPlayerBalance(uuidStr, currency);
                        if (!currentBalanceOpt.has_value()) {
                             output.error(fmt::format("Failed to reduce balance for {}: Account does not exist.", player->getRealName()));
                        } else if (currentBalanceOpt.value() < amountToReduceInCents) { // Compare with cents
                              // 提示余额不足或可能低于最低值
                              output.error(fmt::format("Failed to reduce balance for {}: Insufficient funds (has {}) or result below minimum allowed. Check logs.", player->getRealName(), MoneyManager::formatBalance(currentBalanceOpt.value())));
                         } else {
                             // 可能是数据库错误、低于最低值或其他内部问题
                             output.error(fmt::format("Failed to reduce balance for {}. Result might be below minimum allowed. Check logs.", player->getRealName()));
                         }
                     }
                 }
                sendFeedback(output, fmt::format("Reduced balance for {} players, {} failed.", successCount, failCount), successCount > 0);
            }
        );


    //  4.1 money reduce <playerName> <amount> [currencyType] - 减少离线玩家余额
    moneyCommand.overload<MoneyReduceOfflineArgs>()
        .text("reduce") // 命令文本改为 reduce
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyReduceOfflineArgs const& args, ::Command const&) {
                
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                    output.error("You do not have permission to use this command.");
                    return;
                }
               
                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);
                float inputAmount = args.amount;

                // 验证金额 (要求正数)
                std::optional<int64_t> amountToReduceInCentsOpt = convertCommandFloatToInt64(inputAmount, output, true);
                if (!amountToReduceInCentsOpt) {
                    return;
                }
                int64_t amountToReduceInCents = amountToReduceInCentsOpt.value();

                // Get player info
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString();

                
                // 先检查账户是否存在和余额是否足够，提供更明确的错误信息
                std::optional<int64_t> currentBalanceOpt = moneyManager.getPlayerBalance(uuidStr, currency);
                if (!currentBalanceOpt.has_value()) {
                    sendFeedback(output, fmt::format("Failed to reduce balance for {}: Account does not exist.", playerInfo.name), false);
                    return;
                }
                if (currentBalanceOpt.value() < amountToReduceInCents) {
                    sendFeedback(output, fmt::format("Failed to reduce balance for {}: Insufficient funds (has {}).", playerInfo.name, MoneyManager::formatBalance(currentBalanceOpt.value())), false);
                    return;
                }

                // 尝试扣款 
                std::string reason1 = "Command: cmoney reduce";
                if (moneyManager.subtractPlayerBalance(uuidStr, currency, amountToReduceInCents, reason1)) {
                    int64_t newBalance = currentBalanceOpt.value() - amountToReduceInCents; // 直接计算新余额
                    sendFeedback(output, fmt::format("Reduced {} from {}'s balance ({}). New balance: {}", MoneyManager::formatBalance(amountToReduceInCents), playerInfo.name, currency, MoneyManager::formatBalance(newBalance)), true); // Corrected: Use :: for static method
                } else {
                    // 如果之前的检查都通过了，这里的失败可能是数据库错误或低于最低值
                    sendFeedback(output, fmt::format("Failed to reduce balance for {}. Result might be below minimum allowed. Check logs.", playerInfo.name), false);
                }
            }
        );


    // TODO: 添加其他命令重载，例如：
    // - money query [currencyType] (查询自身余额)
    // - money pay <player> <amount> [currencyType] (转账)
    // - money top [count] [currencyType] (排行榜)

} // registerMoneyCommands function end

} // namespace czmoney
