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
#include "czmoney/money_api.h" // 包含 TransactionLogEntry
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

    // 2. 检查是否要求为正数 (用于 add/reduce/pay)
    if (requirePositive && amount <= 0.0f) {
        // 对于 add/reduce/pay，0 或负数是无效的
        output.error(fmt::format("金额必须为正数，收到: {}", amount));
        return std::nullopt;
    }
    // 移除空的 if 语句块

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
        CommandPermissionLevel::GameDirectors,
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
    // - money top [count] [currencyType] (排行榜)

    // 5. money log [currencyType] - 查询自身流水
    moneyCommand.overload<MoneyLogSelfArgs>()
        .text("log")
        .optional("currencyType") // 可选货币类型
        // .optional("page") // 可选页码
        // .optional("count") // 可选每页数量
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyLogSelfArgs const& args, ::Command const&) {
                // --- 权限检查 (玩家自身查询，通常不需要管理员权限) ---
                // 可以根据需要调整权限级别
                // if (origin.getPermissionsLevel() < CommandPermissionLevel::Any) {
                //     output.error("You do not have permission to use this command.");
                //     return;
                // }

                // --- 检查命令来源是否为玩家 ---
                if (origin.getOriginType() != CommandOriginType::Player) {
                    output.error("This command can only be executed by a player.");
                    return;
                }
                // 尝试从 CommandOrigin 获取 Actor 实体
                Actor* actor = origin.getEntity();
                if (!actor || !actor->isPlayer()) { // 检查 actor 是否有效且为玩家
                    output.error("Failed to get player entity from command origin.");
                    return;
                }
                Player* player = static_cast<Player*>(actor); // 安全地转换为 Player*
                // --- 来源检查结束 ---

                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType); // 获取目标货币类型
                std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID

                // --- 查询流水 ---
                // TODO: 实现分页逻辑 (使用 args.page 和 args.count)
                size_t limit = 20; // 暂时限制最多显示 20 条
                size_t offset = 0; // 暂时从第一条开始
                bool ascendingOrder = false; // 默认降序 (最新在前)

                try {
                    std::vector<TransactionLogEntry> logs = moneyManager.queryTransactionLogs(
                        uuidStr,            // 筛选当前玩家的 UUID
                        currency,           // 筛选指定的货币类型
                        std::nullopt,       // startTimeFilter
                        std::nullopt,       // endTimeFilter
                        std::nullopt,       // reason1Filter
                        std::nullopt,       // reason2Filter
                        std::nullopt,       // reason3Filter
                        limit,              // limit
                        offset,             // offset
                        ascendingOrder      // ascendingOrder
                    );

                    if (logs.empty()) {
                        output.success(fmt::format("No transaction logs found for currency '{}'.", currency));
                        return;
                    }

                    // --- 格式化并发送流水信息 ---
                    output.success(fmt::format("--- Transaction Log ({}) ---", currency));
                    for (const auto& entry : logs) {
                        // 将 double 转换回 int64_t 用于格式化 (使用截断)
                        int64_t changeAmountCents = static_cast<int64_t>(entry.changeAmount * 100.0);
                        int64_t previousAmountCents = static_cast<int64_t>(entry.previousAmount * 100.0);
                        int64_t newAmountCents = previousAmountCents + changeAmountCents; // 计算变动后的金额

                        std::string reasonStr;
                        // 检查 optional 是否有值，并且值不为空
                        if (entry.reason1.has_value() && !entry.reason1.value().empty()) {
                            reasonStr += entry.reason1.value();
                        }
                        if (entry.reason2.has_value() && !entry.reason2.value().empty()) {
                            reasonStr += (reasonStr.empty() ? "" : ", ") + entry.reason2.value();
                        }
                        if (entry.reason3.has_value() && !entry.reason3.value().empty()) {
                            reasonStr += (reasonStr.empty() ? "" : ", ") + entry.reason3.value();
                        }
                        if (reasonStr.empty()) {
                            reasonStr = "N/A"; // 如果所有 reason 都为空或不存在，则显示 N/A
                        }

                        // 格式化输出，包含变动前、变动量、变动后和原因
                        output.success(fmt::format(
                            "[{}] {} -> {} ({}), Reason: {}", // 移除了第四个占位符的 :+
                            entry.timestamp.substr(0, 19), // 截取 YYYY-MM-DD HH:MM:SS
                            MoneyManager::formatBalance(previousAmountCents),
                            MoneyManager::formatBalance(newAmountCents),
                            MoneyManager::formatBalance(changeAmountCents), // formatBalance 已处理符号
                            reasonStr
                        ));
                    }
                    output.success(fmt::format("--- End Log (showing last {}) ---", logs.size()));

                } catch (const std::exception& e) {
                    output.error(fmt::format("Failed to query transaction logs: {}", e.what()));
                }
            }
        );

    // --- 新增 pay 命令 ---
    // 6. money pay <target> <amount> [currencyType] - 转账给在线玩家
    moneyCommand.overload<MoneyPaySelectorArgs>()
        .text("pay")
        .required("target") // 收款人选择器
        .required("amount") // 转账金额
        .optional("currencyType") // 可选货币类型
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyPaySelectorArgs const& args, ::Command const&) {
                // --- 检查命令来源是否为玩家 ---
                if (origin.getOriginType() != CommandOriginType::Player) {
                    output.error("This command can only be executed by a player.");
                    return;
                }
                Actor* senderActor = origin.getEntity();
                if (!senderActor || !senderActor->isPlayer()) {
                    output.error("Failed to get sender player entity.");
                    return;
                }
                Player* senderPlayer = static_cast<Player*>(senderActor);
                std::string senderUuid = senderPlayer->getUuid().asString();
                std::string senderName = senderPlayer->getRealName(); // 获取发送者名称用于反馈
                // --- 来源检查结束 ---

                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);

                // --- 新增：检查此货币类型是否允许转账 ---
                const auto& config = MyMod::getInstance().getConfig();
                auto currencyConfigIt = config.economy.find(currency);
                if (currencyConfigIt == config.economy.end() || !currencyConfigIt->second.allowTransfer) {
                    output.error(fmt::format("Transfers are not allowed for currency type '{}'.", currency));
                    return;
                }
                // --- 检查结束 ---

                float inputAmount = args.amount;

                // 验证转账金额 (必须为正数)
                std::optional<int64_t> amountToTransferOpt = convertCommandFloatToInt64(inputAmount, output, true); // requirePositive = true
                if (!amountToTransferOpt) {
                    return; // 金额无效，错误信息已由辅助函数输出
                }
                int64_t amountToTransfer = amountToTransferOpt.value();

                // 解析收款人选择器
                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("No matching recipient player found.");
                    return;
                }
                if (results.size() > 1) {
                    output.error("Cannot pay multiple players at once. Please specify a single recipient.");
                     return;
                 }
                 // 使用迭代器访问结果
                 Player* receiverPlayer = *results.begin();
                 if (!receiverPlayer) {
                      output.error("Invalid recipient player selected.");
                     return;
                }
                std::string receiverUuid = receiverPlayer->getUuid().asString();
                std::string receiverName = receiverPlayer->getRealName(); // 获取接收者名称

                // --- 防止自己给自己转账 ---
                if (senderUuid == receiverUuid) {
                    output.error("You cannot pay yourself.");
                    return;
                }

                // --- 执行转账 ---
                // 使用 senderName 和 receiverName 作为 reason2 和 reason3
                if (moneyManager.transferBalance(senderUuid, receiverUuid, currency, amountToTransfer, "Transfer", senderName, receiverName)) {
                    // --- 计算税费和实际到账金额 (用于反馈) ---
                    double taxRate = 0.0;
                    // 不需要再次获取 config，因为前面已经获取并检查过了
                    // const auto& currentConfig = MyMod::getInstance().getConfig();
                    // auto currentCurrencyConfigIt = currentConfig.economy.find(currency);
                    if (currencyConfigIt != config.economy.end()) { // 使用前面获取的迭代器
                        taxRate = currencyConfigIt->second.transferTaxRate;
                        if (taxRate < 0.0 || taxRate > 1.0) taxRate = 0.0; // 再次验证
                    }
                    double taxAmountDouble = static_cast<double>(amountToTransfer) * taxRate;
                    int64_t taxAmount = static_cast<int64_t>(std::round(taxAmountDouble));
                     // 确保税费不会导致接收金额小于 0
                    if (taxAmount > amountToTransfer) taxAmount = amountToTransfer;
                    int64_t amountReceived = amountToTransfer - taxAmount;
                    // --- 税费计算结束 (用于反馈) ---


                    // 转账成功反馈 (给发送者)
                    std::string feedbackSender;
                    if (taxAmount > 0) {
                        feedbackSender = fmt::format(
                            "Successfully paid {} {} ({}). Tax: {}. Received by {}: {}",
                            receiverName,
                            MoneyManager::formatBalance(amountToTransfer), // 显示扣除的总额
                            currency,
                            MoneyManager::formatBalance(taxAmount),
                            receiverName, // 明确指出谁收到了
                            MoneyManager::formatBalance(amountReceived)
                        );
                    } else {
                         feedbackSender = fmt::format(
                            "Successfully paid {} {} ({})",
                            receiverName,
                            MoneyManager::formatBalance(amountToTransfer),
                            currency
                        );
                    }
                     sendFeedback(output, feedbackSender, true);

                     // (可选) 给在线的收款人发送通知
                     // 移除 isOnline 检查，直接尝试发送
                     // if (receiverPlayer->isOnline()) { // 确保玩家仍然在线
                          std::string feedbackReceiver;
                          if (taxAmount > 0) {
                               feedbackReceiver = fmt::format(
                                  "You received {} ({}) from {} (Original: {}, Tax: {})",
                                  MoneyManager::formatBalance(amountReceived), // 显示实收金额
                                  currency,
                                  senderName,
                                  MoneyManager::formatBalance(amountToTransfer),
                                  MoneyManager::formatBalance(taxAmount)
                              );
                          } else {
                              feedbackReceiver = fmt::format(
                                  "You received {} ({}) from {}",
                                  MoneyManager::formatBalance(amountReceived), // 等于 amountToTransfer
                                  currency,
                                  senderName
                              );
                          }
                          // 使用 sendMessage 或类似方法发送消息给收款人
                          receiverPlayer->sendMessage(feedbackReceiver);
                          // 或者使用 output.success 但这会显示给命令执行者
                         // output.success(fmt::format("(Notified {})", receiverName));
                    // }

                 } else {
                    // 转账失败反馈 (具体原因已在 MoneyManager 中记录日志)
                    sendFeedback(output, fmt::format("Failed to pay {}. Please check logs for details.", receiverName), false);
                }
            }
        );

    // 7. money pay <playerName> <amount> [currencyType] - 转账给离线玩家
    moneyCommand.overload<MoneyPayOfflineArgs>()
        .text("pay")
        .required("playerName") // 收款人名称
        .required("amount")     // 转账金额
        .optional("currencyType") // 可选货币类型
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyPayOfflineArgs const& args, ::Command const&) {
                 // --- 检查命令来源是否为玩家 ---
                if (origin.getOriginType() != CommandOriginType::Player) {
                    output.error("This command can only be executed by a player.");
                    return;
                }
                Actor* senderActor = origin.getEntity();
                 if (!senderActor || !senderActor->isPlayer()) {
                    output.error("Failed to get sender player entity.");
                    return;
                }
                Player* senderPlayer = static_cast<Player*>(senderActor);
                std::string senderUuid = senderPlayer->getUuid().asString();
                std::string senderName = senderPlayer->getRealName();
                // --- 来源检查结束 ---

                auto& moneyManager = MyMod::getInstance().getMoneyManager();
                std::string currency = getTargetCurrencyType(args.currencyType);

                // --- 新增：检查此货币类型是否允许转账 ---
                const auto& config = MyMod::getInstance().getConfig();
                auto currencyConfigIt = config.economy.find(currency);
                if (currencyConfigIt == config.economy.end() || !currencyConfigIt->second.allowTransfer) {
                    output.error(fmt::format("Transfers are not allowed for currency type '{}'.", currency));
                    return;
                }
                // --- 检查结束 ---

                float inputAmount = args.amount;

                // 验证转账金额 (必须为正数)
                std::optional<int64_t> amountToTransferOpt = convertCommandFloatToInt64(inputAmount, output, true); // requirePositive = true
                if (!amountToTransferOpt) {
                    return;
                }
                int64_t amountToTransfer = amountToTransferOpt.value();

                // 获取收款人信息
                auto receiverInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!receiverInfoOpt.has_value()) {
                    output.error(fmt::format("Recipient player '{}' not found.", args.playerName));
                    return;
                }
                const auto& receiverInfo = receiverInfoOpt.value();
                std::string receiverUuid = receiverInfo.uuid.asString();
                std::string receiverName = receiverInfo.name; // 使用 PlayerInfo 中的名字

                 // --- 防止自己给自己转账 ---
                if (senderUuid == receiverUuid) {
                    output.error("You cannot pay yourself.");
                    return;
                }

                // --- 执行转账 ---
                if (moneyManager.transferBalance(senderUuid, receiverUuid, currency, amountToTransfer, "Transfer", senderName, receiverName)) {
                     // --- 计算税费和实际到账金额 (用于反馈) ---
                     double taxRate = 0.0;
                     // 不需要再次获取 config
                     // const auto& currentConfig = MyMod::getInstance().getConfig();
                     // auto currentCurrencyConfigIt = currentConfig.economy.find(currency);
                     if (currencyConfigIt != config.economy.end()) { // 使用前面获取的迭代器
                         taxRate = currencyConfigIt->second.transferTaxRate;
                         if (taxRate < 0.0 || taxRate > 1.0) taxRate = 0.0; // 再次验证
                     }
                     double taxAmountDouble = static_cast<double>(amountToTransfer) * taxRate;
                     int64_t taxAmount = static_cast<int64_t>(std::round(taxAmountDouble));
                     // 确保税费不会导致接收金额小于 0
                     if (taxAmount > amountToTransfer) taxAmount = amountToTransfer;
                     int64_t amountReceived = amountToTransfer - taxAmount;
                     // --- 税费计算结束 (用于反馈) ---

                    // 转账成功反馈 (给发送者)
                    std::string feedbackSender;
                     if (taxAmount > 0) {
                         feedbackSender = fmt::format(
                             "Successfully paid {} {} ({}). Tax: {}. Received by {}: {}",
                             receiverName,
                             MoneyManager::formatBalance(amountToTransfer), // 显示扣除的总额
                             currency,
                             MoneyManager::formatBalance(taxAmount),
                             receiverName, // 明确指出谁收到了
                             MoneyManager::formatBalance(amountReceived)
                         );
                     } else {
                          feedbackSender = fmt::format(
                             "Successfully paid {} {} ({})",
                             receiverName,
                             MoneyManager::formatBalance(amountToTransfer),
                             currency
                         );
                     }
                    sendFeedback(output, feedbackSender, true);
                    // 注意：无法直接通知离线玩家
                 } else {
                    // 转账失败反馈
                    sendFeedback(output, fmt::format("Failed to pay {}. Please check logs for details.", receiverName), false);
                }
            }
        );


} // registerMoneyCommands function end

} // namespace czmoney
