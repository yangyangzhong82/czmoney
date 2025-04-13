#pragma once // 防止头文件被重复包含

#include <string>      // 使用 std::string
#include <cstdint>     // 使用 int64_t 等固定宽度整数类型
#include <optional>    // 使用 std::optional 表示可能不存在的值
#include "ll/api/io/Logger.h" // 引入 LeviLamina 的日志记录器
#include "czmoney/config.h" // 包含配置文件头文件

// 前向声明 (Forward declaration)
// 这样可以避免在头文件中包含 mysql.h，减少编译依赖
namespace db {
class MySQLConnection;
}

namespace czmoney {

// Config 结构体已包含

/**
 * @brief 管理玩家货币数据的类
 *
 * 负责与数据库交互，处理玩家余额的增删查改操作。
 * 使用整数存储金额（乘以 100），以避免浮点数精度问题。
 */
class MoneyManager {
public:
    /**
     * @brief 构造函数
     * @param dbConn 一个有效的数据库连接对象的引用
     * @param config 配置对象的引用，用于获取初始余额等设置
     */
    explicit MoneyManager(db::MySQLConnection& dbConn, const Config& config); // 修改构造函数签名

    // 禁用拷贝构造函数和拷贝赋值运算符，防止意外复制
    MoneyManager(const MoneyManager&) = delete;
    MoneyManager& operator=(const MoneyManager&) = delete;

    /**
     * @brief 初始化数据库表
     *
     * 检查所需的数据库表是否存在，如果不存在则创建。
     * @return bool 操作是否成功
     */
    bool initializeTable();

    /**
     * @brief 检查玩家账户是否存在
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型 (例如 "money", "points")
     * @return bool 如果账户存在则返回 true，否则返回 false
     */
    bool hasAccount(const std::string& uuid, const std::string& currencyType);

    /**
     * @brief 获取玩家指定货币类型的余额 (不初始化)
     *
     * 余额以整数形式返回，该值是实际金额乘以 100。
     * 如果账户不存在，此函数 *不会* 自动初始化账户。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @return std::optional<int64_t> 如果账户存在，返回余额；否则返回 std::nullopt
     */
    std::optional<int64_t> getPlayerBalance(const std::string& uuid, const std::string& currencyType);

    /**
     * @brief 获取玩家指定货币类型的余额，如果不存在则根据配置初始化
     *
     * 余额以整数形式返回，该值是实际金额乘以 100。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @return int64_t 返回玩家的余额。如果账户不存在，会先初始化再返回初始值。
     * @throws std::runtime_error 如果数据库操作失败或无法获取初始值。
     */
    int64_t getPlayerBalanceOrInit(const std::string& uuid, const std::string& currencyType);

    /**
     * @brief 设置玩家指定货币类型的余额
     *
     * 如果账户不存在，将自动创建。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @param amount 要设置的余额 (整数，实际金额乘以 100)
     * @return bool 操作是否成功
     */
    bool setPlayerBalance(const std::string& uuid, const std::string& currencyType, int64_t amount);

    /**
     * @brief 增加玩家指定货币类型的余额
     *
     * 如果账户不存在，将根据配置自动创建账户并设置初始值，然后在此基础上增加。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @param amountToAdd 要增加的金额 (整数，实际金额乘以 100)
     * @return bool 操作是否成功
     */
    bool addPlayerBalance(const std::string& uuid, const std::string& currencyType, int64_t amountToAdd);

    /**
     * @brief 减少玩家指定货币类型的余额
     *
     * 如果账户不存在或余额不足，操作将失败。此操作 *不会* 初始化账户。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @param amountToSubtract 要减少的金额 (整数，实际金额乘以 100)
     * @return bool 如果操作成功（账户存在且余额足够）则返回 true，否则返回 false
     */
    bool subtractPlayerBalance(const std::string& uuid, const std::string& currencyType, int64_t amountToSubtract);

    /**
     * @brief 将整数余额格式化为带两位小数的字符串
     *
     * 例如：12345 -> "123.45", 50 -> "0.50", 100 -> "1.00"
     * @param amount 整数余额 (实际金额乘以 100)
     * @return std::string 格式化后的字符串
     */
    static std::string formatBalance(int64_t amount);

    /**
     * @brief 将带小数的字符串金额解析为整数余额
     *
     * 例如："123.45" -> 12345, "0.5" -> 50, "1" -> 100
     * 支持一位或两位小数，或无小数。
     * @param formattedAmount 格式化的金额字符串
     * @return std::optional<int64_t> 如果解析成功，返回整数余额；如果格式无效，返回 std::nullopt
     */
    static std::optional<int64_t> parseBalance(const std::string& formattedAmount);


private:
    db::MySQLConnection& mDbConnection; // 持有数据库连接的引用
    const Config& mConfig;             // 持有配置对象的引用 (新增)
    ll::io::Logger& mLogger;           // 持有日志记录器的引用

    /**
     * @brief 检查指定的货币类型是否在配置中定义 (私有辅助函数)
     * @param currencyType 要检查的货币类型
     * @return bool 如果已配置则返回 true，否则返回 false
     */
    bool isCurrencyConfigured(const std::string& currencyType) const;

    /**
     * @brief 初始化指定玩家和货币类型的账户 (私有辅助函数)
     *
     * 根据配置文件中的 initialBalances 设置初始值。
     * 如果配置文件中没有对应的 currencyType，则默认为 0。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @return std::optional<int64_t> 如果初始化成功，返回初始化的余额；否则返回 std::nullopt
     */
    std::optional<int64_t> initializeAccount(const std::string& uuid, const std::string& currencyType);

    // 可以在这里添加其他私有辅助函数
};

} // namespace czmoney
