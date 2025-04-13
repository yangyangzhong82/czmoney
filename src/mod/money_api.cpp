#include "mod/money_api.h" // 包含我们刚刚创建的 API 头文件
#include "mod/MyMod.h"     // 需要访问 MyMod 单例以获取 MoneyManager
#include "mod/money.h"     // 需要调用 MoneyManager 的方法
#include "ll/api/io/Logger.h" // 用于记录日志
#include <stdexcept>       // 用于捕获异常
#include <string>          // MoneyManager 使用 std::string

// 辅助函数，用于安全地获取 MoneyManager 实例并处理异常
inline my_mod::MoneyManager* getMoneyManagerInstance() {
    try {
        return &my_mod::MyMod::getInstance().getMoneyManager();
    } catch (const std::exception& e) {
        // 如果 getMoneyManager() 抛出异常 (例如插件未启用)
        // 使用 MyMod 的 logger 记录错误，因为 API 函数可能在没有特定 logger 上下文的情况下被调用
        try {
             my_mod::MyMod::getInstance().getSelf().getLogger().error("无法获取 MoneyManager 实例 (插件是否已启用?): {}", e.what());
        } catch (...) {
             // 如果连获取 logger 都失败，则无法记录
        }
        return nullptr; // 返回空指针表示失败
    }
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
                 my_mod::MyMod::getInstance().getSelf().getLogger().error("getPlayerBalanceOrInit API 调用失败: {}", e.what());
             } catch (...) {}
             return 0LL; // 发生异常时返回 0 (或者可以考虑其他错误值)
        }
    }
    return 0LL; // 获取 manager 失败时返回 0
}

bool setPlayerBalance(std::string_view uuid, std::string_view currencyType, int64_t amount) {
    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->setPlayerBalance(std::string(uuid), std::string(currencyType), amount);
    }
    return false; // 获取 manager 失败
}

bool addPlayerBalance(std::string_view uuid, std::string_view currencyType, int64_t amountToAdd) {
    if (amountToAdd <= 0) return false; // API 强制要求增加正数金额
    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->addPlayerBalance(std::string(uuid), std::string(currencyType), amountToAdd);
    }
    return false; // 获取 manager 失败
}

bool subtractPlayerBalance(std::string_view uuid, std::string_view currencyType, int64_t amountToSubtract) {
     if (amountToSubtract <= 0) return false; // API 强制要求减少正数金额
    if (auto* manager = getMoneyManagerInstance()) {
        // 注意：将 string_view 转换为 string
        return manager->subtractPlayerBalance(std::string(uuid), std::string(currencyType), amountToSubtract);
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
    return my_mod::MoneyManager::formatBalance(amount);
}

std::optional<int64_t> parseBalance(std::string_view formattedAmount) {
    // 这个函数是静态的，可以直接调用
    // 注意：将 string_view 转换为 string
    return my_mod::MoneyManager::parseBalance(std::string(formattedAmount));
}

} // namespace czmoney::api
