#pragma once // 防止头文件被重复包含

#include <string>      // 使用 std::string
#include <cstdint>     // 使用 int64_t 等固定宽度整数类型
#include <optional>    // 使用 std::optional 表示可能不存在的值
#include <vector>      // 用于返回流水列表
#include "ll/api/io/Logger.h" // 引入 LeviLamina 的日志记录器
#include "czmoney/config.h" // 包含配置文件头文件

// 前向声明 (Forward declaration)
// 这样可以避免在头文件中包含 mysql.h，减少编译依赖
namespace db {
class MySQLConnection;
}

namespace czmoney {

// TransactionLogEntry 结构体现在定义在 money_api.h 中
struct TransactionLogEntry; // 前向声明，如果 MoneyManager 内部只需要指针或引用
                            // 但由于返回类型是 std::vector<TransactionLogEntry>，
                            // 包含 money_api.h 会更直接，或者确保 money.cpp 包含它。
                            // 这里暂时保留前向声明，依赖 money.cpp 包含 money_api.h

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
     * @param reason1 可选的操作理由 1 (例如，插件名称)
     * @param reason2 可选的操作理由 2
     * @param reason3 可选的操作理由 3
     * @return bool 操作是否成功
     */
    bool setPlayerBalance(
        const std::string& uuid,
        const std::string& currencyType,
        int64_t            amount,
        const std::string& reason1 = "",
        const std::string& reason2 = "",
        const std::string& reason3 = ""
    );

    /**
     * @brief 增加玩家指定货币类型的余额
     *
     * 如果账户不存在，将根据配置自动创建账户并设置初始值，然后在此基础上增加。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @param amountToAdd 要增加的金额 (整数，实际金额乘以 100)
     * @param reason1 可选的操作理由 1
     * @param reason2 可选的操作理由 2
     * @param reason3 可选的操作理由 3
     * @return bool 操作是否成功
     */
    bool addPlayerBalance(
        const std::string& uuid,
        const std::string& currencyType,
        int64_t            amountToAdd,
        const std::string& reason1 = "",
        const std::string& reason2 = "",
        const std::string& reason3 = ""
    );

    /**
     * @brief 减少玩家指定货币类型的余额
     *
     * 如果账户不存在或余额不足，操作将失败。此操作 *不会* 初始化账户。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @param amountToSubtract 要减少的金额 (整数，实际金额乘以 100)
     * @param reason1 可选的操作理由 1
     * @param reason2 可选的操作理由 2
     * @param reason3 可选的操作理由 3
     * @return bool 如果操作成功（账户存在且余额足够）则返回 true，否则返回 false
     */
    bool subtractPlayerBalance(
        const std::string& uuid,
        const std::string& currencyType,
        int64_t            amountToSubtract,
        const std::string& reason1 = "",
        const std::string& reason2 = "",
        const std::string& reason3 = ""
    );

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

    /**
     * @brief 查询经济交易流水日志
     *
     * 支持根据多种条件进行筛选。
     * @param uuidFilter 可选，按玩家 UUID 筛选
     * @param currencyTypeFilter 可选，按货币类型筛选
     * @param startTimeFilter 可选，按起始时间筛选 (格式: "YYYY-MM-DD HH:MM:SS")
     * @param endTimeFilter 可选，按结束时间筛选 (格式: "YYYY-MM-DD HH:MM:SS")
     * @param reason1Filter 可选，按理由 1 筛选 (支持 LIKE '%value%')
     * @param reason2Filter 可选，按理由 2 筛选 (支持 LIKE '%value%')
     * @param reason3Filter 可选，按理由 3 筛选 (支持 LIKE '%value%')
     * @param limit 返回的最大记录数，0 表示不限制
     * @param offset 查询结果的偏移量，用于分页
     * @param ascendingOrder 是否按时间升序排序 (默认为 false，即降序)
     * @return std::vector<TransactionLogEntry> 包含符合条件的流水记录列表。查询失败或无结果时返回空列表。
     * @throws std::runtime_error 如果数据库操作失败。
     */
    std::vector<TransactionLogEntry> queryTransactionLogs(
        const std::optional<std::string>& uuidFilter = std::nullopt,
        const std::optional<std::string>& currencyTypeFilter = std::nullopt,
        const std::optional<std::string>& startTimeFilter = std::nullopt,
        const std::optional<std::string>& endTimeFilter = std::nullopt,
        const std::optional<std::string>& reason1Filter = std::nullopt,
        const std::optional<std::string>& reason2Filter = std::nullopt,
        const std::optional<std::string>& reason3Filter = std::nullopt,
        size_t                            limit = 100,
        size_t                            offset = 0,
        bool                              ascendingOrder = false
    );


private:
    /**
     * @brief 安全地将 double 金额转换为 int64_t (分)
     *
     * 处理 NaN, Infinity, 并在转换前检查潜在的溢出。
     * 使用截断方式处理小数部分。
     * @param amount double 类型的金额
     * @param context 用于日志记录的上下文信息 (例如 "initialBalance", "minimumBalance")
     * @return std::optional<int64_t> 转换后的 int64_t 值，如果无效或溢出则返回 std::nullopt
     */
    std::optional<int64_t> convertDoubleToInt64(double amount, const std::string& context) const;

    db::MySQLConnection& mDbConnection; // 持有数据库连接的引用
    const Config& mConfig;             // 持有配置对象的引用 (新增)
    ll::io::Logger& mLogger;           // 持有日志记录器的引用

    /**
     * @brief 初始化经济流水日志表 (私有辅助函数)
     * @return bool 操作是否成功
     */
    bool initializeLogTable();

    /**
     * @brief 记录一笔经济交易流水 
     * @param uuid 玩家 UUID
     * @param currencyType 货币类型
     * @param changeAmount 变动金额 (正数表示增加，负数表示减少)
     * @param previousAmount 操作前的金额
     * @param reason1 理由 1
     * @param reason2 理由 2
     * @param reason3 理由 3
     * @return bool 操作是否成功
     */
    bool logTransaction(
        const std::string& uuid,
        const std::string& currencyType,
        int64_t            changeAmount,
        int64_t            previousAmount,
        const std::string& reason1,
        const std::string& reason2,
        const std::string& reason3
    );


    /**
     * @brief 检查指定的货币类型是否在配置中定义 
     * @param currencyType 要检查的货币类型
     * @return bool 如果已配置则返回 true，否则返回 false
     */
    bool isCurrencyConfigured(const std::string& currencyType) const;

    /**
     * @brief 获取指定货币类型的最低余额
     * @param currencyType 货币类型
     * @return int64_t 最低余额 (整数，实际金额 * 100)。如果配置无效或转换失败，返回 0。
     */
    int64_t getMinimumBalance(const std::string& currencyType) const; // 返回类型不变，但内部实现会转换

    /**
     * @brief 初始化指定玩家和货币类型的账户 
     *
     * 根据配置文件中的 initialBalances 设置初始值。
     * 如果配置文件中没有对应的 currencyType，则默认为 0。
     * @param uuid 玩家的 UUID
     * @param currencyType 货币类型
     * @return std::optional<int64_t> 如果初始化成功，返回初始化的余额 (整数，*100)；否则返回 std::nullopt
     */
    std::optional<int64_t> initializeAccount(const std::string& uuid, const std::string& currencyType); // 返回类型不变，但内部实现会转换

};

} // namespace czmoney
