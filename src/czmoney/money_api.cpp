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
    // 对于截断，需要确保值在 [min_int64, max_int64 + 1) 范围内
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
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    logger.debug("API::getPlayerBalance called for UUID: {}, Currency: {}", uuid, currencyType);

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        std::optional<int64_t> balance = manager->getPlayerBalance(std::string(uuid), std::string(currencyType));
        if (balance) {
            logger.debug("API::getPlayerBalance success for UUID: {}, Currency: {}. Balance: {}", uuid, currencyType, balance.value());
        } else {
            logger.debug("API::getPlayerBalance: Account not found for UUID: {}, Currency: {}", uuid, currencyType);
        }
        return balance;
    } else {
        logger.error("API::getPlayerBalance failed: Could not get MoneyManager instance.");
        return std::nullopt; // 获取 manager 失败
    }
}

int64_t getPlayerBalanceOrInit(std::string_view uuid, std::string_view currencyType) {
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    logger.debug("API::getPlayerBalanceOrInit called for UUID: {}, Currency: {}", uuid, currencyType);

    if (auto* manager = getMoneyManagerInstance()) {
        try {
            // 注意：将 string_view 转换为 string
            int64_t balance = manager->getPlayerBalanceOrInit(std::string(uuid), std::string(currencyType));
            logger.debug("API::getPlayerBalanceOrInit success for UUID: {}, Currency: {}. Balance: {}", uuid, currencyType, balance);
            return balance;
        } catch (const std::exception& e) {
             // 内部 getPlayerBalanceOrInit 应该已经记录了错误，这里记录 API 层面的失败
             logger.error("API::getPlayerBalanceOrInit failed for UUID: {}, Currency: {}. Reason: {}", uuid, currencyType, e.what());
             return 0LL; // 根据函数约定返回 0
        }
    } else {
        logger.error("API::getPlayerBalanceOrInit failed: Could not get MoneyManager instance.");
        return 0LL; // 获取 manager 失败时返回 0
    }
}

// setPlayerBalance 实现
bool setPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amount,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    logger.debug("API::setPlayerBalance called for UUID: {}, Currency: {}, Amount: {}, Reason1: {}, Reason2: {}, Reason3: {}",
                 uuid, currencyType, amount, reason1, reason2, reason3);

    // 转换并验证金额
    std::optional<int64_t> amountInCentsOpt = convertDoubleToInt64(amount, false); // false: 允许负数
    if (!amountInCentsOpt) {
        // convertDoubleToInt64 内部已记录具体原因
        logger.error("API::setPlayerBalance failed for UUID: {}: Invalid amount provided: {}", uuid, amount);
        return false; // 转换或验证失败
    }
    int64_t amountInCents = amountInCentsOpt.value();

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        bool success = manager->setPlayerBalance(
            std::string(uuid),
            std::string(currencyType),
            amountInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
        if (success) {
            logger.debug("API::setPlayerBalance success for UUID: {}, Currency: {}", uuid, currencyType);
        } else {
            // MoneyManager 内部应该记录了具体失败原因
            logger.error("API::setPlayerBalance failed for UUID: {}, Currency: {}. MoneyManager::setPlayerBalance returned false.", uuid, currencyType);
        }
        return success;
    } else {
        logger.error("API::setPlayerBalance failed: Could not get MoneyManager instance.");
        return false; // 获取 manager 失败
    }
}

// addPlayerBalance 实现
bool addPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amountToAdd,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    try {
        auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
        logger.debug("API::addPlayerBalance called for UUID: {}, Currency: {}, AmountToAdd: {}, Reason1: {}, Reason2: {}, Reason3: {}",
                     uuid, currencyType, amountToAdd, reason1, reason2, reason3);

        // 转换并验证金额，要求为正数
        std::optional<int64_t> amountToAddInCentsOpt = convertDoubleToInt64(amountToAdd, true); // true: 要求正数
    if (!amountToAddInCentsOpt) {
        // convertDoubleToInt64 内部已记录具体原因
        logger.error("API::addPlayerBalance failed for UUID: {}: Invalid amount provided: {}", uuid, amountToAdd);
        return false; // 转换或验证失败
    }
    int64_t amountToAddInCents = amountToAddInCentsOpt.value();
    // 检查转换后的值是否 > 0 (因为 convertDoubleToInt64 允许 0 并记录警告)
    if (amountToAddInCents <= 0) {
        logger.error("API::addPlayerBalance failed for UUID: {}: Amount to add must be positive, got {}", uuid, amountToAdd);
        return false;
    }


    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        bool success = manager->addPlayerBalance(
            std::string(uuid),
            std::string(currencyType),
            amountToAddInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
        if (success) {
            logger.debug("API::addPlayerBalance success for UUID: {}, Currency: {}", uuid, currencyType);
        } else {
            // MoneyManager 内部应该记录了具体失败原因
            logger.error("API::addPlayerBalance failed for UUID: {}, Currency: {}. MoneyManager::addPlayerBalance returned false.", uuid, currencyType);
        }
        return success;
    } else {
        logger.error("API::addPlayerBalance failed: Could not get MoneyManager instance.");
        return false; // 获取 manager 失败
        }

    } catch (const std::exception& e) {
        // Catch exceptions potentially from getLogger() or other unexpected places
        fprintf(stderr, "[czmoney API FATAL] Exception in addPlayerBalance: %s\n", e.what());
        try {
            // Attempt to log using the instance if possible, otherwise rely on stderr
            czmoney::MyMod::getInstance().getSelf().getLogger().error("API::addPlayerBalance encountered exception: {}", e.what());
        } catch (...) {} // Ignore logging errors here
        return false; // Indicate failure
    } catch (...) {
        fprintf(stderr, "[czmoney API FATAL] Unknown exception in addPlayerBalance.\n");
         try {
            czmoney::MyMod::getInstance().getSelf().getLogger().error("API::addPlayerBalance encountered unknown exception.");
        } catch (...) {} // Ignore logging errors here
        return false; // Indicate failure
    }
}

// subtractPlayerBalance 实现
bool subtractPlayerBalance(
    std::string_view uuid,
    std::string_view currencyType,
    double           amountToSubtract,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    logger.debug("API::subtractPlayerBalance called for UUID: {}, Currency: {}, AmountToSubtract: {}, Reason1: {}, Reason2: {}, Reason3: {}",
                 uuid, currencyType, amountToSubtract, reason1, reason2, reason3);

    // 转换并验证金额，要求为正数
    std::optional<int64_t> amountToSubtractInCentsOpt = convertDoubleToInt64(amountToSubtract, true); // true: 要求正数
     if (!amountToSubtractInCentsOpt) {
        // convertDoubleToInt64 内部已记录具体原因
        logger.error("API::subtractPlayerBalance failed for UUID: {}: Invalid amount provided: {}", uuid, amountToSubtract);
        return false; // 转换或验证失败
    }
    int64_t amountToSubtractInCents = amountToSubtractInCentsOpt.value();
    // 检查转换后的值是否 > 0 (因为 convertDoubleToInt64 允许 0 并记录警告)
    if (amountToSubtractInCents <= 0) {
        logger.error("API::subtractPlayerBalance failed for UUID: {}: Amount to subtract must be positive, got {}", uuid, amountToSubtract);
        return false;
    }

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        bool success = manager->subtractPlayerBalance(
            std::string(uuid),
            std::string(currencyType),
            amountToSubtractInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
         if (success) {
            logger.debug("API::subtractPlayerBalance success for UUID: {}, Currency: {}", uuid, currencyType);
        } else {
            // MoneyManager 内部应该记录了具体失败原因 (如余额不足)
            logger.error("API::subtractPlayerBalance failed for UUID: {}, Currency: {}. MoneyManager::subtractPlayerBalance returned false.", uuid, currencyType);
        }
        return success;
    } else {
        logger.error("API::subtractPlayerBalance failed: Could not get MoneyManager instance.");
        return false; // 获取 manager 失败
    }
}

bool hasAccount(std::string_view uuid, std::string_view currencyType) {
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    logger.debug("API::hasAccount called for UUID: {}, Currency: {}", uuid, currencyType);

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        bool exists = manager->hasAccount(std::string(uuid), std::string(currencyType));
        logger.debug("API::hasAccount result for UUID: {}, Currency: {}: {}", uuid, currencyType, exists);
        return exists;
    } else {
        logger.error("API::hasAccount failed: Could not get MoneyManager instance.");
        return false; // 获取 manager 失败
    }
}

std::string formatBalance(int64_t amount) {
    // 这个函数是静态的，可以直接调用
    // 可以在 MoneyManager::formatBalance 内部添加日志（如果需要）
    return czmoney::MoneyManager::formatBalance(amount);
}

std::optional<int64_t> parseBalance(std::string_view formattedAmount) {
    // 这个函数是静态的，可以直接调用
    // 可以在 MoneyManager::parseBalance 内部添加日志（如果需要）
    // 注意：将 string_view 转换为 string
    return czmoney::MoneyManager::parseBalance(std::string(formattedAmount));
}

// 实现查询流水 API
std::vector<czmoney::TransactionLogEntry> queryTransactionLogs(
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
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    // 日志记录所有过滤器（如果存在）
    logger.debug("API::queryTransactionLogs called with filters: UUID={}, Currency={}, StartTime={}, EndTime={}, R1={}, R2={}, R3={}, Limit={}, Offset={}, Asc={}",
                 uuidFilter.has_value() ? uuidFilter.value() : "N/A",
                 currencyTypeFilter.has_value() ? currencyTypeFilter.value() : "N/A",
                 startTimeFilter.has_value() ? startTimeFilter.value() : "N/A",
                 endTimeFilter.has_value() ? endTimeFilter.value() : "N/A",
                 reason1Filter.has_value() ? reason1Filter.value() : "N/A",
                 reason2Filter.has_value() ? reason2Filter.value() : "N/A",
                 reason3Filter.has_value() ? reason3Filter.value() : "N/A",
                 limit, offset, ascendingOrder);


    if (auto* manager = getMoneyManagerInstance()) {
        auto convert_sv_opt_to_s_opt = [](const std::optional<std::string_view>& sv_opt) -> std::optional<std::string> {
            if (sv_opt) {
                return std::string(sv_opt.value());
            }
            return std::nullopt;
        };

        try {
            // 将 string_view 过滤器转换为 string 过滤器
            auto uuidStrFilter = convert_sv_opt_to_s_opt(uuidFilter);
            auto currencyStrFilter = convert_sv_opt_to_s_opt(currencyTypeFilter);
            auto startStrFilter = convert_sv_opt_to_s_opt(startTimeFilter);
            auto endStrFilter = convert_sv_opt_to_s_opt(endTimeFilter);
            auto r1StrFilter = convert_sv_opt_to_s_opt(reason1Filter);
            auto r2StrFilter = convert_sv_opt_to_s_opt(reason2Filter);
            auto r3StrFilter = convert_sv_opt_to_s_opt(reason3Filter);

            auto internalLogs = manager->queryTransactionLogs(
                uuidStrFilter,
                currencyStrFilter,
                startStrFilter,
                endStrFilter,
                r1StrFilter,
                r2StrFilter,
                r3StrFilter,
                limit,
                offset,
                ascendingOrder
            );

            logger.debug("API::queryTransactionLogs: MoneyManager returned {} log entries.", internalLogs.size());

            // 2. 将内部日志条目转换为 API 使用的日志条目
            // 假设 money_api.h 中的 TransactionLogEntry 与 money.cpp 内部使用的类型匹配
            std::vector<czmoney::TransactionLogEntry> apiLogs = internalLogs; // 直接赋值

            logger.debug("API::queryTransactionLogs successfully returned {} entries.", apiLogs.size());
            return apiLogs; // 返回列表

        } catch (const std::exception& e) {
             // MoneyManager 内部应该记录了具体错误
             logger.error("API::queryTransactionLogs failed. Reason: {}", e.what());
             return {}; // 返回空向量表示失败
        }
    } else {
        logger.error("API::queryTransactionLogs failed: Could not get MoneyManager instance.");
        return {}; // 获取 manager 失败，返回空向量
    }
}


// 实现转账 API
bool transferBalance(
    std::string_view senderUuid,
    std::string_view receiverUuid,
    std::string_view currencyType,
    double           amountToTransfer,
    std::string_view reason1,
    std::string_view reason2,
    std::string_view reason3
) {
    auto& logger = czmoney::MyMod::getInstance().getSelf().getLogger(); // 获取 logger 实例
    logger.debug("API::transferBalance called: Sender={}, Receiver={}, Currency={}, Amount={}, R1={}, R2={}, R3={}",
                 senderUuid, receiverUuid, currencyType, amountToTransfer, reason1, reason2, reason3);

    // 转换并验证金额，要求为正数
    std::optional<int64_t> amountToTransferInCentsOpt = convertDoubleToInt64(amountToTransfer, true); // true: 要求正数
    if (!amountToTransferInCentsOpt) {
        // convertDoubleToInt64 内部已记录具体原因
        logger.error("API::transferBalance failed: Invalid amount provided: {}", amountToTransfer);
        return false; // 转换或验证失败
    }
    int64_t amountToTransferInCents = amountToTransferInCentsOpt.value();
    // 检查转换后的值是否 > 0 (因为 convertDoubleToInt64 允许 0 并记录警告)
    if (amountToTransferInCents <= 0) {
        logger.error("API::transferBalance failed: Amount to transfer must be positive, got {}", amountToTransfer);
        return false;
    }

    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        bool success = manager->transferBalance(
            std::string(senderUuid),
            std::string(receiverUuid),
            std::string(currencyType),
            amountToTransferInCents,
            std::string(reason1), // 传递理由
            std::string(reason2),
            std::string(reason3)
        );
        if (success) {
            logger.debug("API::transferBalance success: Sender={}, Receiver={}, Currency={}, Amount={}",
                         senderUuid, receiverUuid, currencyType, amountToTransferInCents);
        } else {
            // MoneyManager 内部应该记录了具体失败原因 (如余额不足、不允许转账等)
            logger.error("API::transferBalance failed: Sender={}, Receiver={}, Currency={}, Amount={}. MoneyManager::transferBalance returned false.",
                         senderUuid, receiverUuid, currencyType, amountToTransferInCents);
        }
        return success;
    } else {
        logger.error("API::transferBalance failed: Could not get MoneyManager instance.");
        return false; // 获取 manager 失败
    }
}

} // namespace czmoney::api
