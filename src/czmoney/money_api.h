#pragma once

#include <string>
#include <string_view> // 使用 string_view 提高效率
#include <cstdint>
#include <optional>

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

namespace czmoney::api {

/**
 * @brief 获取玩家指定货币类型的余额 (整数形式，实际金额 * 100)
 *
 * 如果账户不存在，此函数 *不会* 自动初始化账户。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型 (例如 "money", "points")
 * @return std::optional<int64_t> 如果账户存在，返回余额；否则返回 std::nullopt
 */
CZMONEY_API std::optional<int64_t> getPlayerBalance(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 获取玩家指定货币类型的余额，如果不存在则根据配置初始化 (整数形式，实际金额 * 100)
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @return int64_t 返回玩家的余额。如果账户不存在，会先初始化再返回初始值。
 * @note 如果数据库操作失败或无法获取初始值，行为未定义（可能返回 0 或抛出异常，取决于内部实现）。
 *       建议先使用 hasAccount 检查。
 */
CZMONEY_API int64_t getPlayerBalanceOrInit(std::string_view uuid, std::string_view currencyType);

/**
 * @brief 设置玩家指定货币类型的余额 (整数形式，实际金额 * 100)
 *
 * 如果账户不存在，将自动创建。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @param amount 要设置的余额 (浮点数，例如 123.45)
 * @return bool 操作是否成功 (包括金额有效性检查)
 */
CZMONEY_API bool setPlayerBalance(std::string_view uuid, std::string_view currencyType, double amount);

/**
 * @brief 增加玩家指定货币类型的余额
 *
 * 如果账户不存在，将根据配置自动创建账户并设置初始值，然后在此基础上增加。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @param amountToAdd 要增加的金额 (必须为正的浮点数，例如 10.50)
 * @return bool 操作是否成功 (包括金额有效性检查)
 */
CZMONEY_API bool addPlayerBalance(std::string_view uuid, std::string_view currencyType, double amountToAdd);

/**
 * @brief 减少玩家指定货币类型的余额
 *
 * 如果账户不存在或余额不足，操作将失败。此操作 *不会* 初始化账户。
 * @param uuid 玩家的 UUID
 * @param currencyType 货币类型
 * @param amountToSubtract 要减少的金额 (必须为正的浮点数，例如 5.25)
 * @return bool 如果操作成功（账户存在且余额足够）则返回 true，否则返回 false (包括金额有效性检查)
 */
CZMONEY_API bool subtractPlayerBalance(std::string_view uuid, std::string_view currencyType, double amountToSubtract);

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

} // namespace czmoney::api
