#pragma once

#include "ll/api/Config.h" // 包含 LL 的 Config API
#include <string>
#include <vector>
#include <unordered_map> // 包含 unordered_map 头文件
#include <cstdint>       // 包含 int64_t 类型

namespace czmoney {

// 结构体：包含特定货币类型的设置
struct CurrencyConfig {
    // 初始余额 (浮点数，例如 100.00)
    double initialBalance = 0.0;
    // 最低余额 (浮点数，例如 0.00 或 -10.00)，默认为 0.0
    double minimumBalance = 0.0;
    // 是否允许通过 /money pay 命令进行转账
    bool allowTransfer = true; // 默认为允许
    // 转账税率 (0.0 到 1.0 之间的小数，例如 0.05 表示 5%)
    double transferTaxRate = 0.0; // 默认为 0%

    // LL::Config 需要一个序列化/反序列化函数
    template <typename Self>
    void serialize(Self& self) {
        self(initialBalance, "initialBalance");
        self(minimumBalance, "minimumBalance");
        self(allowTransfer, "allowTransfer"); 
        self(transferTaxRate, "transferTaxRate"); 
    }
};

// 主配置结构体
struct Config {
    int version = 1; // 配置文件版本号

    // 数据库类型 ("mysql" 或 "sqlite")
    std::string db_type = "mysql";

    // --- MySQL 连接设置 (仅当 db_type 为 "mysql" 时使用) ---
    std::string db_host     = "127.0.0.1";
    std::string db_user     = "your_username";
    std::string db_password = "your_password";
    std::string db_name     = "your_database";
    unsigned int db_port    = 3306;

    // --- SQLite 连接设置 (仅当 db_type 为 "sqlite" 时使用) ---
    std::string db_sqlite_path = "plugins/czmoney/czmoney.db"; // SQLite 数据库文件路径 (相对路径)

    // 经济设置：按货币类型组织的配置
    // 键: 货币类型 (例如 "money", "points")
    // 值: 该货币类型的具体配置 (CurrencyConfig)
    std::unordered_map<std::string, CurrencyConfig> economy = {
        {"money", {100.00, 0.00, true, 0.05}}, // 示例：money 允许转账，税率 5%
        {"points", {0.0, 0.0, false, 0.0}}     // 示例：points 不允许转账，税率 0%
    };

    // 命令别名设置
    std::vector<std::string> commandAliases = {"cm"}; 


    // LL::Config 需要一个序列化/反序列化函数
    template <typename Self>
    void serialize(Self& self) {
        self(version, "version");
        self(db_type, "database", "type"); // 数据库类型
        // MySQL 设置 (分组)
        self(db_host, "database", "mysql", "host");
        self(db_port, "database", "mysql", "port");
        self(db_user, "database", "mysql", "user");
        self(db_password, "database", "mysql", "password");
        self(db_name, "database", "mysql", "databaseName");
        // SQLite 设置 (分组)
        self(db_sqlite_path, "database", "sqlite", "path");
        // 其他设置
        self(commandAliases, "command", "aliases");
        self(economy, "economy");
    }
};

} // namespace czmoney
