#include "czmoney/money_api.h" 
#include "czmoney/MyMod.h"     
#include "czmoney/money.h"     
#include "ll/api/io/Logger.h" 
#include <stdexcept>      
#include <string>         
#include <cmath>           
#include <limits>         
#include <optional>        
#include <vector>      

// 辅助函数，用于安全地获取 MoneyManager 实例并处理异常
inline czmoney::MoneyManager* getMoneyManagerInstance() {
    try {
        return &czmoney::MyMod::getInstance().getMoneyManager();
    } catch (const std::exception& e) {
        try {
             czmoney::MyMod::getInstance().getSelf().getLogger().error("无法获取 MoneyManager 实例 (插件是否已启用?): {}", e.what());
        } catch (...) {
        }
        return nullptr; // 返回空指针表示失败
    }
}

// 辅助函数：将 double 金额转换为 int64_t (分)，并进行校验 (截断)
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
         try { czmoney::MyMod::getInstance().getSelf().getLogger().debug("API setPlayerBalance 收到负金额: {}", amount); } catch (...) {}
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
bool setPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amount,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    // 转换并验证金额
    std::optional<int64_t> amountInCentsOpt = convertDoubleToInt64(amount, false); // false: 允许负数
    if (!amountInCentsOpt) {
        return false; // 转换或验证失败
    }
    int64_t amountInCents = amountInCentsOpt.value();

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->setPlayerBalance(
            std::string(uuid),
            std::string(currencyType),
            amountInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
    }
    return false; // 获取 manager 失败
}

// 修改 addPlayerBalance 实现
bool addPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amountToAdd,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    // 转换并验证金额，要求为正数
    std::optional<int64_t> amountToAddInCentsOpt = convertDoubleToInt64(amountToAdd, true); // true: 要求正数
    if (!amountToAddInCentsOpt) {
        return false; // 转换或验证失败
    }
    int64_t amountToAddInCents = amountToAddInCentsOpt.value();

    // API 层不再检查 <= 0，交给 convertDoubleToInt64

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->addPlayerBalance(
            std::string(uuid),
            std::string(currencyType),
            amountToAddInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
    }
    return false; // 获取 manager 失败
}

// 修改 subtractPlayerBalance 实现
bool subtractPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amountToSubtract,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    // 转换并验证金额，要求为正数
    std::optional<int64_t> amountToSubtractInCentsOpt = convertDoubleToInt64(amountToSubtract, true); // true: 要求正数
     if (!amountToSubtractInCentsOpt) {
        return false; // 转换或验证失败
    }
    int64_t amountToSubtractInCents = amountToSubtractInCentsOpt.value();

    // API 层不再检查 <= 0，交给 convertDoubleToInt64

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->subtractPlayerBalance(
            std::string(uuid),
            std::string(currencyType),
            amountToSubtractInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
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

// 实现查询流水 API
std::vector<TransactionLogEntry> queryTransactionLogs(
    std::optional<std::string_view> uuidFilter,
    std::optional<std::string_view> currencyTypeFilter,
    std::optional<std::string_view> startTimeFilter,
    std::optional<std::string_view> endTimeFilter,
    std::optional<std::string_view> reason1Filter,
    std::optional<std::string_view> reason2Filter,
    std::optional<std::string_view> reason3Filter,
    size_t                          limit,
    size_t                          offset,
    bool                            ascendingOrder
) {
    if (auto* manager = getMoneyManagerInstance()) {
        // 将 string_view 转换为 string (如果 MoneyManager 接口需要)
        // 注意：MoneyManager 的 queryTransactionLogs 尚未实现，这里假设其接口
        // 使用 std::optional<std::string> 作为过滤器类型
        auto convert_sv_opt_to_s_opt = [](const std::optional<std::string_view>& sv_opt) -> std::optional<std::string> {
            if (sv_opt) {
                return std::string(sv_opt.value());
            }
            return std::nullopt;
        };

        try {
            return manager->queryTransactionLogs(
                convert_sv_opt_to_s_opt(uuidFilter),
                convert_sv_opt_to_s_opt(currencyTypeFilter),
                convert_sv_opt_to_s_opt(startTimeFilter),
                convert_sv_opt_to_s_opt(endTimeFilter),
                convert_sv_opt_to_s_opt(reason1Filter),
                convert_sv_opt_to_s_opt(reason2Filter),
                convert_sv_opt_to_s_opt(reason3Filter),
                limit,
                offset,
                ascendingOrder
            );
        } catch (const std::exception& e) {
            try {
                czmoney::MyMod::getInstance().getSelf().getLogger().error("queryTransactionLogs API 调用失败: {}", e.what());
            } catch (...) {}
            return {}; // 返回空向量表示失败
        }
    }
    return {}; // 获取 manager 失败，返回空向量
}


} // namespace czmoney::api
