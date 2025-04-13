#include "czmoney/money_api.h" // 包含我们刚刚创建的 API 头文件
#include "czmoney/MyMod.h"     // 需要访问 MyMod 单例以获取 MoneyManager
#include "czmoney/money.h"     // 需要调用 MoneyManager 的方法
#include "ll/api/io/Logger.h" // 用于记录日志
#include <stdexcept>       // 用于捕获异常
#include <string>          // MoneyManager 使用 std::string
#include <cmath>           // 用于 std::isnan, std::isinf, std::round
#include <limits>          // 用于 std::numeric_limits
#include <optional>        // 用于 std::optional

// 辅助函数，用于安全地获取 MoneyManager 实例并处理异常
inline czmoney::MoneyManager* getMoneyManagerInstance() {
    try {
        return &czmoney::MyMod::getInstance().getMoneyManager();
    } catch (const std::exception& e) {
        // 如果 getMoneyManager() 抛出异常 (例如插件未启用)
        // 使用 MyMod 的 logger 记录错误，因为 API 函数可能在没有特定 logger 上下文的情况下被调用
        try {
             czmoney::MyMod::getInstance().getSelf().getLogger().error("无法获取 MoneyManager 实例 (插件是否已启用?): {}", e.what());
        } catch (...) {
             // 如果连获取 logger 都失败，则无法记录
        }
        return nullptr; // 返回空指针表示失败
    }
}

// 新增辅助函数：将 double 金额转换为 int64_t (分)，并进行校验 (截断)
std::optional<int64_t> convertDoubleToInt64(double amount, bool requirePositive = false) {
    // 1. 检查 NaN 和 Infinity
    if (std::isnan(amount) || std::isinf(amount)) {
        try {
            czmoney::MyMod::getInstance().getSelf().getLogger().warn("API 调用收到无效金额 (NaN 或 Infinity): {}", amount);
        } catch (...) {}
        return std::nullopt;
    }

    // 2. 检查是否要求为正数 (用于 add/subtract)
    if (requirePositive && amount <= 0.0) {
         try {
            // 对于 add/subtract，允许 0 但记录警告，小于 0 则错误
            if (amount == 0.0) {
                 czmoney::MyMod::getInstance().getSelf().getLogger().warn("API 调用收到零金额，操作无效: {}", amount);
            } else {
                 czmoney::MyMod::getInstance().getSelf().getLogger().error("API 调用要求正金额，收到: {}", amount);
            }
        } catch (...) {}
        return std::nullopt; // 非正数无效
    }
     // 对于 set，允许负数，但检查一下
     if (!requirePositive && amount < 0.0) {
         // 允许负数，无需特殊处理，但可以加日志
         // try { czmoney::MyMod::getInstance().getSelf().getLogger().debug("API setPlayerBalance 收到负金额: {}", amount); } catch (...) {}
     }


    // 3. 转换为分 (不进行四舍五入)
    double centsDouble = amount * 100.0;

    // 4. 检查转换后的值是否在 int64_t 范围内 (截断前的检查)
    // 对于截断，我们需要确保值在 [min_int64, max_int64 + 1) 范围内
    // 因为例如 92233720368547758.07 * 100 = 9223372036854775807.0，截断后是 max_int64
    // 而 -92233720368547758.08 * 100 = -9223372036854775808.0，截断后是 min_int64
    const double min_representable = static_cast<double>(std::numeric_limits<int64_t>::min());
    // 加 1.0 是因为截断，例如 max_int64 + 0.99 截断后仍是 max_int64
    const double max_representable_plus_one = static_cast<double>(std::numeric_limits<int64_t>::max()) + 1.0;

    if (centsDouble < min_representable || centsDouble >= max_representable_plus_one) {
         try {
            czmoney::MyMod::getInstance().getSelf().getLogger().error("API 金额 {} 转换后（截断前）超出 int64_t 可表示范围", amount);
        } catch (...) {}
        return std::nullopt;
    }

    // 5. 安全地转换为 int64_t (执行截断)
    return static_cast<int64_t>(centsDouble);
}


namespace czmoney::api {

// --- API 函数实现 ---

std::optional<int64_t> getPlayerBalance(std::string_view uuid, std::string_view currencyType) {
    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->getPlayerBalance(std::string(uuid), std::string(currencyType));
    }
    return std::nullopt; // 获取 manager 失败
}

int64_t getPlayerBalanceOrInit(std::string_view uuid, std::string_view currencyType) {
    if (auto* manager = getMoneyManagerInstance()) {
        try {
            // 注意：将 string_view 转换为 string
            return manager->getPlayerBalanceOrInit(std::string(uuid), std::string(currencyType));
        } catch (const std::exception& e) {
             try {
                 czmoney::MyMod::getInstance().getSelf().getLogger().error("getPlayerBalanceOrInit API 调用失败: {}", e.what());
             } catch (...) {}
             return 0LL; // 发生异常时返回 0 (或者可以考虑其他错误值)
        }
    }
    return 0LL; // 获取 manager 失败时返回 0
}

// 修改 setPlayerBalance 实现
bool setPlayerBalance(std::string_view uuid, std::string_view currencyType, double amount) {
    // 转换并验证金额
    std::optional<int64_t> amountInCentsOpt = convertDoubleToInt64(amount, false); // false: 允许负数
    if (!amountInCentsOpt) {
        return false; // 转换或验证失败
    }
    int64_t amountInCents = amountInCentsOpt.value();

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->setPlayerBalance(std::string(uuid), std::string(currencyType), amountInCents);
    }
    return false; // 获取 manager 失败
}

// 修改 addPlayerBalance 实现
bool addPlayerBalance(std::string_view uuid, std::string_view currencyType, double amountToAdd) {
    // 转换并验证金额，要求为正数
    std::optional<int64_t> amountToAddInCentsOpt = convertDoubleToInt64(amountToAdd, true); // true: 要求正数
    if (!amountToAddInCentsOpt) {
        return false; // 转换或验证失败
    }
    int64_t amountToAddInCents = amountToAddInCentsOpt.value();

    // API 层不再检查 <= 0，交给 convertDoubleToInt64
    // if (amountToAdd <= 0) return false; // API 强制要求增加正数金额 (已移至转换函数)

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->addPlayerBalance(std::string(uuid), std::string(currencyType), amountToAddInCents);
    }
    return false; // 获取 manager 失败
}

// 修改 subtractPlayerBalance 实现
bool subtractPlayerBalance(std::string_view uuid, std::string_view currencyType, double amountToSubtract) {
    // 转换并验证金额，要求为正数
    std::optional<int64_t> amountToSubtractInCentsOpt = convertDoubleToInt64(amountToSubtract, true); // true: 要求正数
     if (!amountToSubtractInCentsOpt) {
        return false; // 转换或验证失败
    }
    int64_t amountToSubtractInCents = amountToSubtractInCentsOpt.value();

    // API 层不再检查 <= 0，交给 convertDoubleToInt64
    // if (amountToSubtract <= 0) return false; // API 强制要求减少正数金额 (已移至转换函数)

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->subtractPlayerBalance(std::string(uuid), std::string(currencyType), amountToSubtractInCents);
    }
    return false; // 获取 manager 失败
}

bool hasAccount(std::string_view uuid, std::string_view currencyType) {
    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->hasAccount(std::string(uuid), std::string(currencyType));
    }
    return false; // 获取 manager 失败
}

std::string formatBalance(int64_t amount) {
    // 这个函数是静态的，可以直接调用
    return czmoney::MoneyManager::formatBalance(amount);
}

std::optional<int64_t> parseBalance(std::string_view formattedAmount) {
    // 这个函数是静态的，可以直接调用
    // 注意：将 string_view 转换为 string
    return czmoney::MoneyManager::parseBalance(std::string(formattedAmount));
}

} // namespace czmoney::api
