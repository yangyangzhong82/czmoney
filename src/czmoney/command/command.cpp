#include "czmoney/command/command.h"
#include "czmoney/MyMod.h"
#include "czmoney/config.h"
#include "czmoney/money/money.h" // 仍然需要 MoneyManager::formatBalance 的声明，因为 convertCommandFloatToInt64 内部使用了它，但会改为 API 调用
#include "czmoney/money/money_api.h" // 包含 TransactionLogEntry 和 API 函数
#include "czmoney/ui/Transfer.h"     // 包含 TransferForm
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/EnumName.h"
#include "ll/api/command/SoftEnum.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/server/commands/Command.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace czmoney {

using ll::command::CommandHandle;
using ll::command::CommandRegistrar;
using ll::command::SoftEnum;
using ll::service::PlayerInfo;

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
        output.error(fmt::format("无效的金额输入：'{}' 不是一个有效的数字。", amount));
        return std::nullopt;
    }

    // 2. 检查是否要求为正数 (用于 add/reduce/pay)
    if (requirePositive && amount <= 0.0f) {
        // 对于 add/reduce/pay，0 或负数是无效的
        output.error(fmt::format("金额必须是正数，您输入了 '{}'。", amount));
        return std::nullopt;
    }
    // 移除空的 if 语句块

    // 3. 转换为分 (不进行四舍五入)
    // 先转 double 提高精度
    double amountDouble = static_cast<double>(amount);
    double centsDouble  = amountDouble * 100.0;

    // 4. 检查转换后的值是否在 int64_t 范围内 (截断前的检查)
    const double min_representable          = static_cast<double>(std::numeric_limits<int64_t>::min());
    const double max_representable_plus_one = static_cast<double>(std::numeric_limits<int64_t>::max()) + 1.0;

    if (centsDouble < min_representable || centsDouble >= max_representable_plus_one) {
        // 使用更精确的范围提示
        output.error(fmt::format(
            "金额 '{}' 转换后超出有效范围。有效范围为 [{}, {}]。",
            amount,
            czmoney::api::formatBalance(std::numeric_limits<int64_t>::min()), // 格式化最小值
            czmoney::api::formatBalance(std::numeric_limits<int64_t>::max())  // 格式化最大值
        ));
        return std::nullopt;
    }

    // 5. 安全地转换为 int64_t (执行截断)
    return static_cast<int64_t>(centsDouble);
}

// 修改函数签名以接受别名列表
void registerMoneyCommands(const std::vector<std::string>& aliases) {
    auto& registrar = CommandRegistrar::getInstance();
    auto& logger    = MyMod::getInstance().getSelf().getLogger(); // 获取 logger

    // --- 注册/更新 SoftEnum ---
    const auto&              config = MyMod::getInstance().getConfig(); // 获取配置
    std::vector<std::string> currencyTypes;                             // 存储从配置中读取的货币类型
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
        CommandFlagValue::NotCheat // 无需作弊
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
        .text("query")            // 子命令名称
        .required("target")       // 目标选择器参数
        .optional("currencyType") // 可选的货币类型参数 (现在是 SoftEnum)
        .execute([](CommandOrigin const&          origin,
                    CommandOutput&                output,
                    MoneyQuerySelectorArgs const& args,
                    ::Command const&) {
            // --- Permission Check ---
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("您没有权限使用此命令。");
                return;
            }
            // --- End Permission Check ---
            std::string currency = getTargetCurrencyType(args.currencyType);
            auto        results  = args.target.results(origin); // 解析目标选择器

            if (results.empty()) {
                output.error("未找到匹配的玩家。");
                return;
            }

            for (Player* player : results) {
                if (!player) continue; // 跳过无效的玩家指针

                std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                // 调用 API 层
                double balance = czmoney::api::getPlayerBalanceOrInit(uuidStr, currency); // 获取或初始化余额

                // 构建反馈消息
                std::string feedback = fmt::format(
                    "玩家 {} 的余额 ({}): {}",
                    player->getRealName(),
                    currency,
                    czmoney::api::formatBalance(static_cast<int64_t>(balance * 100.0)
                    ) // API 返回 double，需要转回 int64_t 格式化
                );
                sendFeedback(output, feedback, true); // 发送成功反馈
            }
        });

    //  1.1 money query <playerName> [currencyType] - 查询离线玩家余额
    moneyCommand.overload<MoneyQueryOfflineArgs>()
        .text("query")
        .required("playerName")
        .optional("currencyType")
        .execute([](CommandOrigin const&         origin,
                    CommandOutput&               output,
                    MoneyQueryOfflineArgs const& args,
                    ::Command const&) {
            // --- Permission Check ---
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("您没有权限使用此命令。");
                return;
            }
            // --- End Permission Check ---
            std::string currency = getTargetCurrencyType(args.currencyType);

            // 使用 PlayerInfo 获取玩家信息
            auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
            if (!playerInfoOpt.has_value()) {
                output.error(fmt::format("未找到玩家 '{}'。", args.playerName));
                return;
            }
            const auto& playerInfo = playerInfoOpt.value();
            std::string uuidStr    = playerInfo.uuid.asString(); // 获取 UUID

            // 调用 API 层
            double balance = czmoney::api::getPlayerBalanceOrInit(uuidStr, currency); // 获取或初始化余额

            // 构建反馈消息
            std::string feedback = fmt::format(
                "玩家 {} 的余额 ({}): {}",
                playerInfo.name, // 使用 PlayerInfo 中的名字
                currency,
                czmoney::api::formatBalance(static_cast<int64_t>(balance * 100.0)
                ) // API 返回 double，需要转回 int64_t 格式化
            );
            sendFeedback(output, feedback, true); // 发送成功反馈
        });


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
                    output.error("您没有权限使用此命令。");
                    return;
                }
                // --- End Permission Check ---

                std::string currency    = getTargetCurrencyType(args.currencyType);
                float       inputAmount = args.amount; // Get the float amount

                // API 接受 double，所以这里直接用 float 转换成 double 传递
                double amountDouble = static_cast<double>(inputAmount);

                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("未找到匹配的玩家。");
                    return;
                }

                // 获取命令执行者的名称作为理由
                std::string reason2 = "Console"; // 默认理由为控制台
                if (origin.getOriginType() == CommandOriginType::Player) {
                    Actor* actor = origin.getEntity();
                    if (actor && actor->isPlayer()) {
                        Player* playerOrigin = static_cast<Player*>(actor);
                        reason2              = playerOrigin->getRealName(); // 如果是玩家，则获取玩家名称
                    }
                }

                int successCount = 0;
                int failCount    = 0;
                for (Player* player : results) {
                    if (!player) {
                        failCount++;
                        continue;
                    }
                    std::string uuidStr = player->getUuid().asString();
                    // 添加理由
                    std::string                  reason1 = "Command: cmoney set";
                    czmoney::api::MoneyApiResult result =
                        czmoney::api::setPlayerBalance(uuidStr, currency, amountDouble, reason1, reason2); // 调用 API
                    if (result == czmoney::api::MoneyApiResult::Success) {
                        successCount++;
                    } else {
                        failCount++;
                        std::string errorMessage;
                        switch (result) {
                        case czmoney::api::MoneyApiResult::InvalidAmount:
                            errorMessage = "无效金额。";
                            break;
                        case czmoney::api::MoneyApiResult::DatabaseError:
                            errorMessage = "数据库操作失败。";
                            break;
                        case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                            errorMessage = "经济系统不可用。";
                            break;
                        case czmoney::api::MoneyApiResult::AccountNotFound:
                        case czmoney::api::MoneyApiResult::InsufficientBalance:
                        case czmoney::api::MoneyApiResult::UnknownError:
                        default:
                            errorMessage = "未知错误。";
                            break;
                        }
                        output.error(fmt::format(
                            "为玩家 {} 设置余额失败：{}。请查看日志获取详细信息。",
                            player->getRealName(),
                            errorMessage
                        ));
                    }
                }
                sendFeedback(
                    output,
                    fmt::format("成功为 {} 名玩家设置了余额，{} 名玩家失败。", successCount, failCount),
                    successCount > 0
                );
            }
        );

    //  2.1 money set <playerName> <amount> [currencyType] - 设置离线玩家余额
    moneyCommand.overload<MoneySetOfflineArgs>()
        .text("set")
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute([](CommandOrigin const&       origin,
                    CommandOutput&             output,
                    MoneySetOfflineArgs const& args,
                    ::Command const&) {
            // --- Permission Check ---
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("您没有权限使用此命令。");
                return;
            }
            // --- End Permission Check ---
            std::string currency     = getTargetCurrencyType(args.currencyType);
            float       inputAmount  = args.amount;
            double      amountDouble = static_cast<double>(inputAmount);

            // Get player info
            auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
            if (!playerInfoOpt.has_value()) {
                output.error(fmt::format("未找到玩家 '{}'。", args.playerName));
                return;
            }
            const auto& playerInfo = playerInfoOpt.value();
            std::string uuidStr    = playerInfo.uuid.asString();

            // Set balance with reason
            std::string                  reason1 = "Command: cmoney set";
            czmoney::api::MoneyApiResult result =
                czmoney::api::setPlayerBalance(uuidStr, currency, amountDouble, reason1); // 调用 API
            if (result == czmoney::api::MoneyApiResult::Success) {
                sendFeedback(
                    output,
                    fmt::format(
                        "成功将玩家 {} 的余额 ({}) 设置为 {}.",
                        playerInfo.name,
                        currency,
                        czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0))
                    ),
                    true
                ); // Corrected: Use API for formatBalance
            } else {
                std::string errorMessage;
                switch (result) {
                case czmoney::api::MoneyApiResult::InvalidAmount:
                    errorMessage = "无效金额。";
                    break;
                case czmoney::api::MoneyApiResult::DatabaseError:
                    errorMessage = "数据库操作失败。";
                    break;
                case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                    errorMessage = "经济系统不可用。";
                    break;
                case czmoney::api::MoneyApiResult::AccountNotFound:
                case czmoney::api::MoneyApiResult::InsufficientBalance:
                case czmoney::api::MoneyApiResult::UnknownError:
                default:
                    errorMessage = "未知错误。";
                    break;
                }
                sendFeedback(
                    output,
                    fmt::format("为玩家 {} 设置余额失败：{}。请查看日志获取详细信息。", playerInfo.name, errorMessage),
                    false
                );
            }
        });

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
                    output.error("您没有权限使用此命令。");
                    return;
                }
                // --- End Permission Check ---
                std::string currency     = getTargetCurrencyType(args.currencyType);
                float       inputAmount  = args.amount; // Get the float amount
                double      amountDouble = static_cast<double>(inputAmount);

                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("未找到匹配的玩家。");
                    return;
                }

                // 获取命令执行者的名称作为理由
                std::string reason2 = "Console"; // 默认理由为控制台
                if (origin.getOriginType() == CommandOriginType::Player) {
                    Actor* actor = origin.getEntity();
                    if (actor && actor->isPlayer()) {
                        Player* playerOrigin = static_cast<Player*>(actor);
                        reason2              = playerOrigin->getRealName(); // 如果是玩家，则获取玩家名称
                    }
                }

                int successCount = 0;
                int failCount    = 0;
                for (Player* player : results) {
                    if (!player) {
                        failCount++;
                        continue;
                    }
                    std::string uuidStr = player->getUuid().asString();
                    // 添加理由
                    std::string                  reason1 = "Command: cmoney add";
                    czmoney::api::MoneyApiResult result =
                        czmoney::api::addPlayerBalance(uuidStr, currency, amountDouble, reason1, reason2); // 调用 API
                    if (result == czmoney::api::MoneyApiResult::Success) {
                        successCount++;
                    } else {
                        failCount++;
                        std::string errorMessage;
                        switch (result) {
                        case czmoney::api::MoneyApiResult::InvalidAmount:
                            errorMessage = "无效金额。";
                            break;
                        case czmoney::api::MoneyApiResult::DatabaseError:
                            errorMessage = "数据库操作失败。";
                            break;
                        case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                            errorMessage = "经济系统不可用。";
                            break;
                        case czmoney::api::MoneyApiResult::AccountNotFound:
                        case czmoney::api::MoneyApiResult::InsufficientBalance:
                        case czmoney::api::MoneyApiResult::UnknownError:
                        default:
                            errorMessage = "未知错误。";
                            break;
                        }
                        output.error(fmt::format(
                            "为玩家 {} 增加余额失败：{}。请查看日志获取详细信息。",
                            player->getRealName(),
                            errorMessage
                        ));
                    }
                }
                sendFeedback(
                    output,
                    fmt::format("成功为 {} 名玩家增加了余额，{} 名玩家失败。", successCount, failCount),
                    successCount > 0
                );
            }
        );

    //  3.1 money add <playerName> <amount> [currencyType] - 增加离线玩家余额
    moneyCommand.overload<MoneyAddOfflineArgs>()
        .text("add")
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute([](CommandOrigin const&       origin,
                    CommandOutput&             output,
                    MoneyAddOfflineArgs const& args,
                    ::Command const&) {
            // --- Permission Check ---
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("您没有权限使用此命令。");
                return;
            }
            // --- End Permission Check ---
            std::string currency     = getTargetCurrencyType(args.currencyType);
            float       inputAmount  = args.amount;
            double      amountDouble = static_cast<double>(inputAmount);

            // Get player info
            auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
            if (!playerInfoOpt.has_value()) {
                output.error(fmt::format("未找到玩家 '{}'。", args.playerName));
                return;
            }
            const auto& playerInfo = playerInfoOpt.value();
            std::string uuidStr    = playerInfo.uuid.asString();
            std::string reason2    = "Console"; // 默认理由为控制台
            if (origin.getOriginType() == CommandOriginType::Player) {
                Actor* actor = origin.getEntity();
                if (actor && actor->isPlayer()) {
                    Player* playerOrigin = static_cast<Player*>(actor);
                    reason2              = playerOrigin->getRealName(); // 如果是玩家，则获取玩家名称
                }
            }
            // Add balance with reason
            std::string                  reason1 = "Command: cmoney add";
            czmoney::api::MoneyApiResult result =
                czmoney::api::addPlayerBalance(uuidStr, currency, amountDouble, reason1, reason2); // 调用 API
            if (result == czmoney::api::MoneyApiResult::Success) {
                // Get the new balance to display it
                double newBalance =
                    czmoney::api::getPlayerBalanceOrInit(uuidStr, currency); // Re-fetch or assume success
                sendFeedback(
                    output,
                    fmt::format(
                        "成功为玩家 {} 增加了 {} ({}). 新余额: {}",
                        playerInfo.name,
                        czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)),
                        currency,
                        czmoney::api::formatBalance(static_cast<int64_t>(newBalance * 100.0))
                    ),
                    true
                ); // Corrected: Use API for formatBalance
            } else {
                std::string errorMessage;
                switch (result) {
                case czmoney::api::MoneyApiResult::InvalidAmount:
                    errorMessage = "无效金额。";
                    break;
                case czmoney::api::MoneyApiResult::DatabaseError:
                    errorMessage = "数据库操作失败。";
                    break;
                case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                    errorMessage = "经济系统不可用。";
                    break;
                case czmoney::api::MoneyApiResult::AccountNotFound:
                case czmoney::api::MoneyApiResult::InsufficientBalance:
                case czmoney::api::MoneyApiResult::UnknownError:
                default:
                    errorMessage = "未知错误。";
                    break;
                }
                sendFeedback(
                    output,
                    fmt::format("为玩家 {} 增加余额失败：{}。请查看日志获取详细信息。", playerInfo.name, errorMessage),
                    false
                );
            }
        });

    // 4. money reduce <target> <amount> [currencyType] - 减少在线玩家余额
    moneyCommand.overload<MoneyReduceSelectorArgs>()
        .text("reduce") // 命令文本改为 reduce (或 subtract)
        .required("target")
        .required("amount")
        .optional("currencyType") // 可选的货币类型参数 (现在是 SoftEnum)
        .execute([](CommandOrigin const&           origin,
                    CommandOutput&                 output,
                    MoneyReduceSelectorArgs const& args,
                    ::Command const&) {
            // --- Permission Check ---
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("您没有权限使用此命令。");
                return;
            }
            // --- End Permission Check ---
            std::string currency     = getTargetCurrencyType(args.currencyType);
            float       inputAmount  = args.amount; // Get the float amount
            double      amountDouble = static_cast<double>(inputAmount);

            auto results = args.target.results(origin);
            if (results.empty()) {
                output.error("未找到匹配的玩家。");
                return;
            }

            // 获取命令执行者的名称作为理由
            std::string reason2 = "Console"; // 默认理由为控制台
            if (origin.getOriginType() == CommandOriginType::Player) {
                Actor* actor = origin.getEntity();
                if (actor && actor->isPlayer()) {
                    Player* playerOrigin = static_cast<Player*>(actor);
                    reason2              = playerOrigin->getRealName(); // 如果是玩家，则获取玩家名称
                }
            }

            int successCount = 0;
            int failCount    = 0;
            for (Player* player : results) {
                if (!player) {
                    failCount++;
                    continue;
                }
                std::string uuidStr = player->getUuid().asString();
                // 添加理由
                std::string                  reason1 = "Command: cmoney reduce";
                czmoney::api::MoneyApiResult result =
                    czmoney::api::subtractPlayerBalance(uuidStr, currency, amountDouble, reason1, reason2); // 调用 API
                if (result == czmoney::api::MoneyApiResult::Success) {
                    successCount++;
                } else {
                    failCount++;
                    std::string errorMessage;
                    switch (result) {
                    case czmoney::api::MoneyApiResult::InvalidAmount:
                        errorMessage = "无效金额。";
                        break;
                    case czmoney::api::MoneyApiResult::DatabaseError:
                        errorMessage = "数据库操作失败。";
                        break;
                    case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                        errorMessage = "经济系统不可用。";
                        break;
                    case czmoney::api::MoneyApiResult::AccountNotFound:
                        errorMessage = "账户不存在。";
                        break;
                    case czmoney::api::MoneyApiResult::InsufficientBalance:
                        // 获取当前余额以提供更详细的错误信息
                        {
                            std::optional<double> currentBalanceOpt = czmoney::api::getPlayerBalance(uuidStr, currency);
                            if (currentBalanceOpt.has_value()) {
                                errorMessage = fmt::format(
                                    "余额不足 (拥有 {})。",
                                    czmoney::api::formatBalance(static_cast<int64_t>(currentBalanceOpt.value() * 100.0))
                                );
                            } else {
                                errorMessage = "余额不足。";
                            }
                        }
                        break;
                    case czmoney::api::MoneyApiResult::UnknownError:
                    default:
                        errorMessage = "未知错误。";
                        break;
                    }
                    output.error(fmt::format(
                        "为玩家 {} 减少余额失败：{}。请查看日志获取详细信息。",
                        player->getRealName(),
                        errorMessage
                    ));
                }
            }
            sendFeedback(
                output,
                fmt::format("成功为 {} 名玩家减少了余额，{} 名玩家失败。", successCount, failCount),
                successCount > 0
            );
        });


    //  4.1 money reduce <playerName> <amount> [currencyType] - 减少离线玩家余额
    moneyCommand.overload<MoneyReduceOfflineArgs>()
        .text("reduce") // 命令文本改为 reduce
        .required("playerName")
        .required("amount")
        .optional("currencyType")
        .execute([](CommandOrigin const&          origin,
                    CommandOutput&                output,
                    MoneyReduceOfflineArgs const& args,
                    ::Command const&) {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("您没有权限使用此命令。");
                return;
            }

            std::string currency     = getTargetCurrencyType(args.currencyType);
            float       inputAmount  = args.amount;
            double      amountDouble = static_cast<double>(inputAmount);

            // Get player info
            auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
            if (!playerInfoOpt.has_value()) {
                output.error(fmt::format("未找到玩家 '{}'。", args.playerName));
                return;
            }
            const auto& playerInfo = playerInfoOpt.value();
            std::string uuidStr    = playerInfo.uuid.asString();

            // 先检查账户是否存在和余额是否足够，提供更明确的错误信息
            std::optional<double> currentBalanceOpt = czmoney::api::getPlayerBalance(uuidStr, currency);
            if (!currentBalanceOpt.has_value()) {
                sendFeedback(output, fmt::format("为玩家 {} 减少余额失败：账户不存在。", playerInfo.name), false);
                return;
            }
            if (currentBalanceOpt.value() < amountDouble) { // 直接比较 double
                sendFeedback(
                    output,
                    fmt::format(
                        "为玩家 {} 减少余额失败：余额不足 (拥有 {})。",
                        playerInfo.name,
                        czmoney::api::formatBalance(static_cast<int64_t>(currentBalanceOpt.value() * 100.0))
                    ),
                    false
                );
                return;
            }
            // 获取命令执行者的名称作为理由
            std::string reason2 = "Console"; // 默认理由为控制台
            if (origin.getOriginType() == CommandOriginType::Player) {
                Actor* actor = origin.getEntity();
                if (actor && actor->isPlayer()) {
                    Player* playerOrigin = static_cast<Player*>(actor);
                    reason2              = playerOrigin->getRealName(); // 如果是玩家，则获取玩家名称
                }
            }
            // 尝试扣款
            std::string                  reason1 = "Command: cmoney reduce";
            czmoney::api::MoneyApiResult result =
                czmoney::api::subtractPlayerBalance(uuidStr, currency, amountDouble, reason1, reason2); // 调用 API
            if (result == czmoney::api::MoneyApiResult::Success) {
                double newBalance = currentBalanceOpt.value() - amountDouble; // 直接计算新余额
                sendFeedback(
                    output,
                    fmt::format(
                        "成功从玩家 {} 的余额 ({}) 中减少了 {}。新余额: {}",
                        playerInfo.name,
                        currency,
                        czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)),
                        czmoney::api::formatBalance(static_cast<int64_t>(newBalance * 100.0))
                    ),
                    true
                ); // Corrected: Use API for formatBalance
            } else {
                std::string errorMessage;
                switch (result) {
                case czmoney::api::MoneyApiResult::InvalidAmount:
                    errorMessage = "无效金额。";
                    break;
                case czmoney::api::MoneyApiResult::DatabaseError:
                    errorMessage = "数据库操作失败。";
                    break;
                case czmoney::api::MoneyApiResult::MoneyManagerNotAvailable:
                    errorMessage = "经济系统不可用。";
                    break;
                case czmoney::api::MoneyApiResult::AccountNotFound:
                    errorMessage = "账户不存在。";
                    break;
                case czmoney::api::MoneyApiResult::InsufficientBalance:
                    errorMessage = "余额不足。";
                    break;
                case czmoney::api::MoneyApiResult::UnknownError:
                default:
                    errorMessage = "未知错误。";
                    break;
                }
                sendFeedback(
                    output,
                    fmt::format("为玩家 {} 减少余额失败：{}。请查看日志获取详细信息。", playerInfo.name, errorMessage),
                    false
                );
            }
        });


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
                //     output.error("您没有权限使用此命令。");
                //     return;
                // }

                // --- 检查命令来源是否为玩家 ---
                if (origin.getOriginType() != CommandOriginType::Player) {
                    output.error("此命令只能由玩家执行。");
                    return;
                }
                // 尝试从 CommandOrigin 获取 Actor 实体
                Actor* actor = origin.getEntity();
                if (!actor || !actor->isPlayer()) { // 检查 actor 是否有效且为玩家
                    output.error("无法从命令源获取玩家实体。");
                    return;
                }
                Player* player = static_cast<Player*>(actor); // 安全地转换为 Player*
                // --- 来源检查结束 ---

                std::string currency = getTargetCurrencyType(args.currencyType); // 获取目标货币类型
                std::string uuidStr  = player->getUuid().asString();             // 获取玩家 UUID

                // --- 查询流水 ---
                // TODO: 实现分页逻辑 (使用 args.page 和 args.count)
                size_t limit          = 20;    // 暂时限制最多显示 20 条
                size_t offset         = 0;     // 暂时从第一条开始
                bool   ascendingOrder = false; // 默认降序 (最新在前)

                try {
                    // 调用 API 层
                    std::vector<czmoney::TransactionLogEntry> logs = czmoney::api::queryTransactionLogs(
                        uuidStr,       // 筛选当前玩家的 UUID
                        currency,      // 筛选指定的货币类型
                        std::nullopt,  // startTimeFilter
                        std::nullopt,  // endTimeFilter
                        std::nullopt,  // reason1Filter
                        std::nullopt,  // reason2Filter
                        std::nullopt,  // reason3Filter
                        limit,         // limit
                        offset,        // offset
                        ascendingOrder // ascendingOrder
                    );

                    if (logs.empty()) {
                        output.success(fmt::format("未找到货币 '{}' 的交易日志。", currency));
                        return;
                    }

                    // --- 格式化并发送流水信息 ---
                    output.success(fmt::format("--- 交易日志 ({}) ---", currency));
                    for (const auto& entry : logs) {
                        // API 返回的 changeAmount 和 previousAmount 已经是 double (元)
                        // 需要转换为 int64_t (分) 再格式化
                        int64_t changeAmountCents   = static_cast<int64_t>(entry.changeAmount * 100.0);
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
                            "[{}] {} -> {} ({}), 原因: {}", // 移除了第四个占位符的 :+
                            entry.timestamp.substr(0, 19),  // 截取 YYYY-MM-DD HH:MM:SS
                            czmoney::api::formatBalance(previousAmountCents),
                            czmoney::api::formatBalance(newAmountCents),
                            czmoney::api::formatBalance(changeAmountCents), // formatBalance 已处理符号
                            reasonStr
                        ));
                    }
                    output.success(fmt::format("--- 日志结束 (显示最近 {} 条) ---", logs.size()));

                } catch (const std::exception& e) {
                    output.error(fmt::format("查询交易日志失败：{}", e.what()));
                }
            }
        );

    // --- 新增 pay 命令 ---
    // 新增：money pay (无参数) - 打开转账表单
    moneyCommand
        .overload() // 无参数重载
        .text("pay")
        .execute([&logger](CommandOrigin const& origin, CommandOutput& output) { // 显式捕获 logger
            // --- 检查命令来源是否为玩家 ---
            if (origin.getOriginType() != CommandOriginType::Player) {
                output.error("此命令只能由玩家执行。");
                return;
            }
            Actor* actor = origin.getEntity();
            if (!actor || !actor->isPlayer()) {
                output.error("无法从命令源获取玩家实体。");
                return;
            }
            Player* player = static_cast<Player*>(actor);
            // --- 来源检查结束 ---

            // 获取配置中的默认货币类型
            const auto& config              = MyMod::getInstance().getConfig();
            std::string defaultCurrencyType = "money"; // 默认值
            if (config.economy.count("money")) {       // 检查 "money" 是否存在于配置中
                defaultCurrencyType = "money";         // 确保使用配置中存在的货币类型
            } else if (!config.economy.empty()) {
                // 如果 "money" 不存在，使用第一个允许转账的货币类型
                for (const auto& pair : config.economy) {
                    if (pair.second.allowTransfer) {
                        defaultCurrencyType = pair.first;
                        break;
                    }
                }
            } else {
                logger.warn("配置中未找到任何货币类型，将使用默认 'money'。");
            }

            try {

                czmoney::ui::showTransferForm(
                    *player,
                    "",
                    0,
                    defaultCurrencyType
                ); // 调用辅助函数显示转账表单，并传递默认货币类型
                output.success("正在打开转账表单...");
            } catch (const std::exception& e) {
                output.error(fmt::format("打开转账表单失败：{}", e.what()));
            }
        });

    // 6. money pay <target> <amount> [currencyType] - 转账给在线玩家
    moneyCommand.overload<MoneyPaySelectorArgs>()
        .text("pay")
        .required("target")       // 收款人选择器
        .required("amount")       // 转账金额
        .optional("currencyType") // 可选货币类型
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyPaySelectorArgs const& args, ::Command const&) {
                // --- 检查命令来源是否为玩家 ---
                if (origin.getOriginType() != CommandOriginType::Player) {
                    output.error("此命令只能由玩家执行。");
                    return;
                }
                Actor* senderActor = origin.getEntity();
                if (!senderActor || !senderActor->isPlayer()) {
                    output.error("无法获取发送者玩家实体。");
                    return;
                }
                Player*     senderPlayer = static_cast<Player*>(senderActor);
                std::string senderUuid   = senderPlayer->getUuid().asString();
                std::string senderName   = senderPlayer->getRealName(); // 获取发送者名称用于反馈
                // --- 来源检查结束 ---

                std::string currency = getTargetCurrencyType(args.currencyType);

                // --- 新增：检查此货币类型是否允许转账 ---
                const auto& config           = MyMod::getInstance().getConfig();
                auto        currencyConfigIt = config.economy.find(currency);
                if (currencyConfigIt == config.economy.end() || !currencyConfigIt->second.allowTransfer) {
                    output.error(fmt::format("货币类型 '{}' 不允许转账。", currency));
                    return;
                }
                // --- 检查结束 ---

                float  inputAmount  = args.amount;
                double amountDouble = static_cast<double>(inputAmount);

                // 解析收款人选择器
                auto results = args.target.results(origin);
                if (results.empty()) {
                    output.error("未找到匹配的收款玩家。");
                    return;
                }
                if (results.size() > 1) {
                    output.error("无法同时向多个玩家转账。请指定单个收款人。");
                    return;
                }
                // 使用迭代器访问结果
                Player* receiverPlayer = *results.begin();
                if (!receiverPlayer) {
                    output.error("选择了无效的收款玩家。");
                    return;
                }
                std::string receiverUuid = receiverPlayer->getUuid().asString();
                std::string receiverName = receiverPlayer->getRealName(); // 获取接收者名称

                // --- 防止自己给自己转账 ---
                if (senderUuid == receiverUuid) {
                    output.error("您不能给自己转账。");
                    return;
                }

                // --- 执行转账 ---
                // 使用 senderName 和 receiverName 作为 reason2 和 reason3
                czmoney::api::MoneyApiResult transferResult = czmoney::api::transferBalance(
                    senderUuid,
                    receiverUuid,
                    currency,
                    amountDouble,
                    "Transfer",
                    senderName,
                    receiverName
                ); // 调用 API
                if (transferResult == czmoney::api::MoneyApiResult::Success) {
                    // --- 计算税费和实际到账金额 (用于反馈) ---
                    double taxRate = 0.0;
                    if (currencyConfigIt != config.economy.end()) { // 使用前面获取的迭代器
                        taxRate = currencyConfigIt->second.transferTaxRate;
                        if (taxRate < 0.0 || taxRate > 1.0) taxRate = 0.0; // 再次验证
                    }
                    double  taxAmountDouble = amountDouble * taxRate;
                    int64_t taxAmount =
                        static_cast<int64_t>(std::round(taxAmountDouble * 100.0)); // 转为分再格式化
                                                                                   // 确保税费不会导致接收金额小于 0
                    if (taxAmount > static_cast<int64_t>(amountDouble * 100.0))
                        taxAmount = static_cast<int64_t>(amountDouble * 100.0);
                    int64_t amountReceivedCents = static_cast<int64_t>(amountDouble * 100.0) - taxAmount;
                    // --- 税费计算结束 (用于反馈) ---


                    // 转账成功反馈 (给发送者)
                    std::string feedbackSender;
                    if (taxAmount > 0) {
                        feedbackSender = fmt::format(
                            "成功向 {} 转账 {} ({})。税费: {}。{} 实际收到: {}",
                            receiverName,
                            czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)), // 显示扣除的总额
                            currency,
                            czmoney::api::formatBalance(taxAmount),
                            receiverName, // 明确指出谁收到了
                            czmoney::api::formatBalance(amountReceivedCents)
                        );
                    } else {
                        feedbackSender = fmt::format(
                            "成功向 {} 转账 {} ({})。",
                            receiverName,
                            czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)),
                            currency
                        );
                    }
                    sendFeedback(output, feedbackSender, true);

                    std::string feedbackReceiver;
                    if (taxAmount > 0) {
                        feedbackReceiver = fmt::format(
                            "您从 {} 收到了 {} ({}) (原始金额: {}, 税费: {})。",
                            senderName,
                            czmoney::api::formatBalance(amountReceivedCents), // 显示实收金额
                            currency,
                            czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)),
                            czmoney::api::formatBalance(taxAmount)
                        );
                    } else {
                        feedbackReceiver = fmt::format(
                            "您从 {} 收到了 {} ({})。",
                            senderName,
                            czmoney::api::formatBalance(amountReceivedCents), // 等于 amountToTransfer
                            currency
                        );
                    }
                    receiverPlayer->sendMessage(feedbackReceiver);
                    // }

                } else {
                    // 转账失败反馈 (具体原因已在 MoneyManager 中记录日志)
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
                        errorMessage = "账户不存在。";
                        break;
                    case czmoney::api::MoneyApiResult::UnknownError:
                    default:
                        errorMessage = "未知错误。";
                        break;
                    }
                    sendFeedback(
                        output,
                        fmt::format("向 {} 转账失败：{}。请查看日志获取详细信息。", receiverName, errorMessage),
                        false
                    );
                }
            }
        );

    // 7. money pay <playerName> <amount> [currencyType] - 转账给离线玩家
    moneyCommand.overload<MoneyPayOfflineArgs>()
        .text("pay")
        .required("playerName")   // 收款人名称
        .required("amount")       // 转账金额
        .optional("currencyType") // 可选货币类型
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, MoneyPayOfflineArgs const& args, ::Command const&) {
                // --- 检查命令来源是否为玩家 ---
                if (origin.getOriginType() != CommandOriginType::Player) {
                    output.error("此命令只能由玩家执行。");
                    return;
                }
                Actor* senderActor = origin.getEntity();
                if (!senderActor || !senderActor->isPlayer()) {
                    output.error("无法获取发送者玩家实体。");
                    return;
                }
                Player*     senderPlayer = static_cast<Player*>(senderActor);
                std::string senderUuid   = senderPlayer->getUuid().asString();
                std::string senderName   = senderPlayer->getRealName();
                // --- 来源检查结束 ---

                std::string currency = getTargetCurrencyType(args.currencyType);

                // --- 新增：检查此货币类型是否允许转账 ---
                const auto& config           = MyMod::getInstance().getConfig();
                auto        currencyConfigIt = config.economy.find(currency);
                if (currencyConfigIt == config.economy.end() || !currencyConfigIt->second.allowTransfer) {
                    output.error(fmt::format("货币类型 '{}' 不允许转账。", currency));
                    return;
                }
                // --- 检查结束 ---

                float  inputAmount  = args.amount;
                double amountDouble = static_cast<double>(inputAmount);

                // 获取收款人信息
                auto receiverInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!receiverInfoOpt.has_value()) {
                    output.error(fmt::format("未找到收款玩家 '{}'。", args.playerName));
                    return;
                }
                const auto& receiverInfo = receiverInfoOpt.value();
                std::string receiverUuid = receiverInfo.uuid.asString();
                std::string receiverName = receiverInfo.name; // 使用 PlayerInfo 中的名字

                // --- 防止自己给自己转账 ---
                if (senderUuid == receiverUuid) {
                    output.error("您不能给自己转账。");
                    return;
                }

                // --- 执行转账 ---
                czmoney::api::MoneyApiResult transferResult = czmoney::api::transferBalance(
                    senderUuid,
                    receiverUuid,
                    currency,
                    amountDouble,
                    "Transfer",
                    senderName,
                    receiverName
                ); // 调用 API
                if (transferResult == czmoney::api::MoneyApiResult::Success) {
                    // --- 计算税费和实际到账金额 (用于反馈) ---
                    double taxRate = 0.0;
                    if (currencyConfigIt != config.economy.end()) { // 使用前面获取的迭代器
                        taxRate = currencyConfigIt->second.transferTaxRate;
                        if (taxRate < 0.0 || taxRate > 1.0) taxRate = 0.0; // 再次验证
                    }
                    double taxAmountDouble = amountDouble * taxRate;
                    int64_t taxAmount = static_cast<int64_t>(std::round(taxAmountDouble * 100.0)); // 转为分再格式化
                    // 确保税费不会导致接收金额小于 0
                    if (taxAmount > static_cast<int64_t>(amountDouble * 100.0))
                        taxAmount = static_cast<int64_t>(amountDouble * 100.0);
                    int64_t amountReceivedCents = static_cast<int64_t>(amountDouble * 100.0) - taxAmount;
                    // --- 税费计算结束 (用于反馈) ---

                    // 转账成功反馈 (给发送者)
                    std::string feedbackSender;
                    if (taxAmount > 0) {
                        feedbackSender = fmt::format(
                            "成功向 {} 转账 {} ({})。税费: {}。{} 实际收到: {}",
                            receiverName,
                            czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)), // 显示扣除的总额
                            currency,
                            czmoney::api::formatBalance(taxAmount),
                            receiverName, // 明确指出谁收到了
                            czmoney::api::formatBalance(amountReceivedCents)
                        );
                    } else {
                        feedbackSender = fmt::format(
                            "成功向 {} 转账 {} ({})。",
                            receiverName,
                            czmoney::api::formatBalance(static_cast<int64_t>(amountDouble * 100.0)),
                            currency
                        );
                    }
                    sendFeedback(output, feedbackSender, true);
                    // 注意：无法直接通知离线玩家
                } else {
                    // 转账失败反馈
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
                        errorMessage = "账户不存在。";
                        break;
                    case czmoney::api::MoneyApiResult::UnknownError:
                    default:
                        errorMessage = "未知错误。";
                        break;
                    }
                    sendFeedback(
                        output,
                        fmt::format("向 {} 转账失败：{}。请查看日志获取详细信息。", receiverName, errorMessage),
                        false
                    );
                }
            }
        );

    // 8. money rank [currencyType] - 查看排行榜
    moneyCommand.overload<MoneyRankArgs>()
        .text("rank")
        .optional("currencyType") // 可选货币类型
        .execute([](CommandOrigin const& origin, CommandOutput& output, MoneyRankArgs const& args, ::Command const&) {
            // --- 检查命令来源是否为玩家 ---
            if (origin.getOriginType() != CommandOriginType::Player) {
                output.error("此命令只能由玩家执行。");
                return;
            }
            Actor* actor = origin.getEntity();
            if (!actor || !actor->isPlayer()) {
                output.error("无法从命令源获取玩家实体。");
                return;
            }
            Player* player = static_cast<Player*>(actor);
            // --- 来源检查结束 ---

            // TODO: 根据 args.currencyType 筛选排行榜，目前 RankForm 内部硬编码了 "money"
            // 如果需要支持不同货币类型的排行榜，RankForm 的构造函数需要修改以接受 currencyType
            // 目前，我们直接打开表单，表单内部会处理默认货币类型

            try {
                czmoney::ui::RankForm form(*player); // 实例化排行榜表单
                // 表单在构造函数中已经发送，这里不需要额外调用 sendTo
                output.success("正在打开排行榜表单...");
            } catch (const std::exception& e) {
                output.error(fmt::format("打开排行榜表单失败：{}", e.what()));
            }
        });


} // registerMoneyCommands function end

} // namespace czmoney
