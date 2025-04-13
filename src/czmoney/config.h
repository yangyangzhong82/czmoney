#pragma once

#include <string>
#include <unordered_map> // 包含 unordered_map 头文件
#include <cstdint>       // 包含 int64_t 类型

namespace czmoney {

// 结构体：包含特定货币类型的设置
struct CurrencyConfig {
    // 初始余额 (整数，实际金额 * 100)
    int64_t initialBalance = 0;

    // 未来可以添加更多设置，例如：
    // bool enabled = true;
    // double transactionFee = 0.0;
    // std::string displayName = "";
};

// 主配置结构体
struct Config {
    int version = 1; // 配置文件版本号

    // 数据库连接设置
    std::string db_host     = "127.0.0.1";
    std::string db_user     = "your_username";
    std::string db_password = "your_password";
    std::string db_name     = "your_database";
    unsigned int db_port    = 3306;

    // 经济设置：按货币类型组织的配置
    // 键: 货币类型 (例如 "money", "points")
    // 值: 该货币类型的具体配置 (CurrencyConfig)
    std::unordered_map<std::string, CurrencyConfig> economy = {
        {"money", {10000}}, // 示例：money 的初始值为 100.00
        {"points", {0}}     // 示例：points 的初始值为 0
        // 可以添加更多货币类型及其配置
        // {"gems", {5, true, 0.01, "Gems"}} // 假设 CurrencyConfig 有更多字段
    };

    // 未来可以添加其他全局配置
    // std::string defaultCurrencyType = "money";
};

} // namespace czmoney
