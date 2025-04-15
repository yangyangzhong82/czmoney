#pragma once

#include "mc/server/commands/CommandSelector.h" // 包含 CommandSelector
#include "mc/world/actor/player/Player.h"       // 包含 Player
#include "ll/api/command/SoftEnum.h" // 新增: 包含 SoftEnum
#include <string>
#include <cstdint> // for int64_t

namespace czmoney {

// 定义一个空的枚举类作为 SoftEnum 的基础
// 这个枚举本身不包含任何值，值将从配置动态加载
enum class CurrencyTypeEnum {};

// --- 命令参数结构体 ---

// 用于查询余额 (带目标选择器)
struct MoneyQuerySelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 修改类型: 货币类型 (可选)
};

// 用于查询离线玩家余额
struct MoneyQueryOfflineArgs {
    std::string             playerName;     // 玩家名称
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 货币类型 (可选)
};

// 用于设置余额 (带目标选择器)
struct MoneySetSelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    float                   amount;         // 金额 (浮点数形式)
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 修改类型: 货币类型 (可选)
};

// 用于设置离线玩家余额
struct MoneySetOfflineArgs {
    std::string             playerName;     // 玩家名称
    float                   amount;         // 金额 (浮点数形式)
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 货币类型 (可选)
};

// 用于增加余额 (带目标选择器)
struct MoneyAddSelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    float                   amount;         // 金额 (浮点数形式)
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 修改类型: 货币类型 (可选)
};

// 用于增加离线玩家余额
struct MoneyAddOfflineArgs {
    std::string             playerName;     // 玩家名称
    float                   amount;         // 金额 (浮点数形式)
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 货币类型 (可选)
};

// 用于减少余额 (带目标选择器)
struct MoneyReduceSelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    float                   amount;         // 金额 (浮点数形式)
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 修改类型: 货币类型 (可选)
};

// 用于减少离线玩家余额
struct MoneyReduceOfflineArgs {
    std::string             playerName;     // 玩家名称
    float                   amount;         // 金额 (浮点数形式)
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 货币类型 (可选)
};

// 用于查询自身流水
struct MoneyLogSelfArgs {
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType; // 可选的货币类型
    // 可以考虑添加分页参数
    // int page = 1;
    // int count = 10;
};

// --- 新增：用于转账给在线玩家 ---
struct MoneyPaySelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器 (收款人)
    float                   amount;         // 转账金额
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 货币类型 (可选)
};

// --- 新增：用于转账给离线玩家 ---
struct MoneyPayOfflineArgs {
    std::string             playerName;     // 目标玩家名称 (收款人)
    float                   amount;         // 转账金额
    ll::command::SoftEnum<CurrencyTypeEnum> currencyType;   // 货币类型 (可选)
};



// --- 命令注册函数声明 ---

/**
 * @brief 注册所有与经济相关的命令
 * @param aliases 要为根命令注册的别名列表
 */
void registerMoneyCommands(const std::vector<std::string>& aliases);

} // namespace czmoney
