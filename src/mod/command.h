#pragma once

#include "mc/server/commands/CommandSelector.h" // 包含 CommandSelector
#include "mc/world/actor/player/Player.h"       // 包含 Player
#include <string>
#include <cstdint> // for int64_t

namespace my_mod {

// 前向声明 Player 类 (如果 CommandSelector.h 或 Player.h 没有完全包含定义)
// class Player;

// --- 命令参数结构体 ---

// 用于查询余额 (带目标选择器)
struct MoneyQuerySelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    std::string             currencyType;   // 货币类型 (可选)
};

// 用于设置余额 (带目标选择器)
struct MoneySetSelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    float                   amount;         // 金额 (浮点数形式)
    std::string             currencyType;   // 货币类型 (可选)
};

// 用于增加余额 (带目标选择器)
struct MoneyAddSelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    float                   amount;         // 金额 (浮点数形式)
    std::string             currencyType;   // 货币类型 (可选)
};

// 用于减少余额 (带目标选择器)
struct MoneyReduceSelectorArgs {
    CommandSelector<Player> target;         // 目标玩家选择器
    float                   amount;         // 金额 (浮点数形式)
    std::string             currencyType;   // 货币类型 (可选)
};


// --- 命令注册函数声明 ---

/**
 * @brief 注册所有与经济相关的命令
 */
void registerMoneyCommands();

} // namespace my_mod
