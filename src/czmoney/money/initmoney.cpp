#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "czmoney/MyMod.h" // 包含 MyMod 头文件以访问 MoneyManager 和配置
#include "czmoney/config.h" // 包含 config 头文件以获取经济类型

namespace czmoney {
void initMoney() {
    // 注册事件监听器
    ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerJoinEvent>(
        [&](ll::event::player::PlayerJoinEvent& ev) {
            // 在这里处理事件
            auto& player = ev.self();
            std::string uuidStr = player.getUuid().asString(); // 获取玩家 UUID 字符串

            auto& myMod = MyMod::getInstance();
            auto& moneyManager = myMod.getMoneyManager();
            const auto& config = myMod.getConfig(); // 获取配置

            // 遍历所有经济类型并初始化玩家余额
            for (const auto& pair : config.economy) {
                const std::string& currencyType = pair.first;
                moneyManager.getPlayerBalanceOrInit(uuidStr, currencyType);
            }
        }
    );
}
} // namespace czmoney
