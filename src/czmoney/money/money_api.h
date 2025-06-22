#pragma once

#include <string>
#include <string_view> // 使用 string_view 提高效率
#include <cstdint>
#include <optional>
#include <vector>      // 用于返回多个条目
#include <utility> // For std::pair

// 定义导出/导入宏
// 当编译 czmoney 插件本身时，应定义 CZMONEY_API_EXPORTS
// 其他插件包含此头文件时，不定义 CZMONEY_API_EXPORTS
#ifdef CZMONEY_API_EXPORTS
    #if defined(_MSC_VER)
        #define CZMONEY_API __declspec(dllexport)
    #elif defined(__GNUC__) || defined(__clang__)
        #define CZMONEY_API __attribute__((visibility("default")))
    #else
        #define CZMONEY_API
    #endif
#else
    #if defined(_MSC_VER)
        #define CZMONEY_API __declspec(dllimport)
    #else
        #define CZMONEY_API
    #endif
#endif

namespace czmoney {
// 前向声明 TransactionLogEntry，其定义现在位于 money.h 中
struct TransactionLogEntry;
} // namespace czmoney

namespace czmoney::api {

// 定义 API 操作结果的枚举
enum class MoneyApiResult {
    Success = 0,
    AccountNotFound,            // 账户不存在
    InvalidAmount,              // 无效金额 (例如 NaN, Infinity, 负数用于增加/减少, 超出范围)
    InsufficientBalance,        // 余额不足
    DatabaseError,              // 数据库操作失败
    MoneyManagerNotAvailable,   // MoneyManager 实例不可用 (插件未启用或初始化失败)
    UnknownError                // 未知错误
};

/**
 * @brief 获取玩家指定货币类型的余额 (浮点数形式，实际金额)
 *
 * 如果账户不存在，此函数 *不会* 自动初始化账户。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型 (例如 "money", "points")
 * @return std::optional<double> 如果账户存在，返回余额；否则返回 std::nullopt
 */
CZMONEY_API std::optional<double> getPlayerBalance(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 获取玩家指定货币类型的原始余额 (整数形式，实际金额 * 100)
 *
 * 如果账户不存在，此函数 *不会* 自动初始化账户。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型 (例如 "money", "points")
 * @return std::optional<int64_t> 如果账户存在，返回原始余额；否则返回 std::nullopt
 */
CZMONEY_API std::optional<int64_t> getRawPlayerBalance(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 获取玩家指定货币类型的余额，如果不存在则根据配置初始化 (浮点数形式，实际金额)
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @return double 返回玩家的余额。如果账户不存在，会先初始化再返回初始值。
 * @note 如果数据库操作失败或无法获取初始值，行为未定义（可能返回 0 或抛出异常，取决于内部实现）。
 *       建议先使用 hasAccount 检查。
 */
CZMONEY_API double getPlayerBalanceOrInit(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 获取玩家指定货币类型的原始余额，如果不存在则根据配置初始化 (整数形式，实际金额 * 100)
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @return int64_t 返回玩家的原始余额。如果账户不存在，会先初始化再返回初始值。
 */
CZMONEY_API int64_t getRawPlayerBalanceOrInit(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 设置玩家指定货币类型的余额 (浮点数形式，实际金额)
 *
 * 如果账户不存在，将自动创建。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @param amount 要设置的余额 (浮点数，例如 123.45)
 * @param reason1 可选的操作理由 1 (例如，插件名称)
 * @param reason2 可选的操作理由 2
 * @param reason3 可选的操作理由 3
 * @return MoneyApiResult 操作结果
 */
CZMONEY_API MoneyApiResult setPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amount,
    std::string_view reason1 = "",
    std::string_view reason2 = "",
    std::string_view reason3 = ""
);

/**
 * @brief 增加玩家指定货币类型的余额
 *
 * 如果账户不存在，将根据配置自动创建账户并设置初始值，然后在此基础上增加。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @param amountToAdd 要增加的金额 (必须为正的浮点数，例如 10.50)
 * @param reason1 可选的操作理由 1
 * @param reason2 可选的操作理由 2
 * @param reason3 可选的操作理由 3
 * @return MoneyApiResult 操作结果
 */
CZMONEY_API MoneyApiResult addPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amountToAdd,
    std::string_view reason1 = "",
    std::string_view reason2 = "",
    std::string_view reason3 = ""
);

/**
 * @brief 减少玩家指定货币类型的余额
 *
 * 如果账户不存在或余额不足，操作将失败。此操作 *不会* 初始化账户。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @param amountToSubtract 要减少的金额 (必须为正的浮点数，例如 5.25)
 * @param reason1 可选的操作理由 1
 * @param reason2 可选的操作理由 2
 * @param reason3 可选的操作理由 3
 * @return MoneyApiResult 操作结果
 */
CZMONEY_API MoneyApiResult subtractPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amountToSubtract,
    std::string_view reason1 = "",
    std::string_view reason2 = "",
    std::string_view reason3 = ""
);

/**
 * @brief 检查玩家账户是否存在
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @return bool 如果账户存在则返回 true，否则返回 false
 */
CZMONEY_API bool hasAccount(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 将整数余额格式化为带两位小数的字符串
 * @param amount 整数余额 (实际金额 * 100)
 * @return std::string 格式化后的字符串
 */
CZMONEY_API std::string formatBalance(int64_t amount);

/**
 * @brief 将带小数的字符串金额解析为整数余额
 * @param formattedAmount 格式化的金额字符串
 * @return std::optional<int64_t> 如果解析成功，返回整数余额；如果格式无效，返回 std::nullopt
 */
CZMONEY_API std::optional<int64_t> parseBalance(std::string_view formattedAmount);

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
 */
CZMONEY_API std::vector<czmoney::TransactionLogEntry> queryTransactionLogs(
    std::optional<std::string_view> uuidFilter = std::nullopt,
    std::optional<std::string_view> currencyTypeFilter = std::nullopt,
    std::optional<std::string_view> startTimeFilter = std::nullopt,
    std::optional<std::string_view> endTimeFilter = std::nullopt,
    std::optional<std::string_view> reason1Filter = std::nullopt,
    std::optional<std::string_view> reason2Filter = std::nullopt,
    std::optional<std::string_view> reason3Filter = std::nullopt,
    size_t                          limit = 100, // 默认限制 100 条
    size_t                          offset = 0,
    bool                            ascendingOrder = false
);

/**
 * @brief 从一个玩家向另一个玩家转账
 *
 * 原子性地执行扣款和加款操作。
 * @param senderUuid 转出方玩家 UUID
 * @param receiverUuid 接收方玩家 UUID
 * @param currencyType 货币类型
 * @param amountToTransfer 要转账的金额 (必须为正的浮点数，例如 10.50)
 * @param reason1 可选的操作理由 1 (例如 "Transfer")
 * @param reason2 可选的操作理由 2 (例如 发起者名称)
 * @param reason3 可选的操作理由 3 (例如 接收者名称)
 * @return MoneyApiResult 操作结果
 */
CZMONEY_API MoneyApiResult transferBalance(
    std::string_view senderUuid,
    std::string_view receiverUuid,
    std::string_view currencyType,
    double           amountToTransfer,
    std::string_view reason1 = "Transfer",
    std::string_view reason2 = "",
    std::string_view reason3 = ""
);

/**
 * @brief 获取指定货币类型的金币排行榜数据
 *
 * 查询 player_balances 表，按金额降序排列。
 * @param currencyType 货币类型
 * @param limit 返回的最大记录数
 * @param offset 查询结果的偏移量
 * @return std::vector<std::pair<std::string, int64_t>> 包含 UUID 和余额（整数，实际金额 * 100）的列表
 */
CZMONEY_API std::vector<std::pair<std::string, int64_t>> getTopBalances(
    std::string_view currencyType,
    size_t limit = 10,
    size_t offset = 0
);


} // namespace czmoney::api
