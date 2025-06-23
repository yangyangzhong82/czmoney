#include "czmoney/event/AddMoneyEvent.h" // 包含我们自己的事件头文件
#include "czmoney/event/SetMoneyEvent.h"
#include "czmoney/event/SubtractMoneyEvent.h"
#include "czmoney/event/TransferMoneyEvent.h" // 新增：转账事件头文件
#include "czmoney/money/money.h"
#include "czmoney/money/money_api.h" // 包含 API 头文件，用于触发事件
#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"


namespace czmoney::test {

/**
 * @brief 注册用于测试经济事件的监听器
 *
 * 这个函数会设置监听器来捕获 AddMoneyBeforeEvent 和 AddMoneyAfterEvent，
 * 并打印出详细的事件信息。
 */
void registerMoneyEventListeners() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("正在注册 czmoney 事件测试监听器...");

    // 1. 监听 AddMoneyBeforeEvent (事前事件)
    ll::event::EventBus::getInstance().emplaceListener<event::AddMoneyBeforeEvent>(
        [&logger](event::AddMoneyBeforeEvent& event) {
            logger.info("--- AddMoneyBeforeEvent 触发 ---");
            logger.info("  玩家 UUID: {}", event.getPlayerUuid());
            logger.info("  货币类型: {}", event.getCurrencyType());
            logger.info("  计划增加金额 (原始): {}", MoneyManager::formatBalance(event.getAmountToAdd()));
            logger.info("  理由 1: '{}'", event.getReason1());
            logger.info("  理由 2: '{}'", event.getReason2());
            logger.info("  理由 3: '{}'", event.getReason3());

            // --- 测试逻辑：修改和取消 ---
            // 示例 1: 如果理由是 "TEST_DOUBLE"，则金额翻倍
            if (event.getReason1() == "TEST_DOUBLE") {
                int64_t originalAmount  = event.getAmountToAdd();
                event.getAmountToAdd() *= 2;
                logger.warn(
                    "  [测试] 金额翻倍！从 {} 增加到 {}",
                    MoneyManager::formatBalance(originalAmount),
                    MoneyManager::formatBalance(event.getAmountToAdd())
                );
            }

            // 示例 2: 如果理由是 "TEST_CANCEL"，则取消事件
            if (event.getReason1() == "TEST_CANCEL") {
                event.cancel();
                logger.warn("  [测试] 此事件已被取消！");
            }
        },
        ll::event::EventPriority::Normal, // 优先级
        ll::mod::NativeMod::current()     // 监听器所有者
    );

    // 2. 监听 AddMoneyAfterEvent (事后事件)
    ll::event::EventBus::getInstance().emplaceListener<event::AddMoneyAfterEvent>(
        [&logger](event::AddMoneyAfterEvent& event) {
            logger.info("--- AddMoneyAfterEvent 触发 ---");
            logger.info("  玩家 UUID: {}", event.getPlayerUuid());
            logger.info("  货币类型: {}", event.getCurrencyType());
            logger.info("  实际增加金额: {}", MoneyManager::formatBalance(event.getAmountToAdd()));
            logger.info("  理由 1: '{}'", event.getReason1());
            logger.info("  理由 2: '{}'", event.getReason2());
            logger.info("  理由 3: '{}'", event.getReason3());
            logger.info("------------------------------------");
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );
    // --- SetMoneyEvent 监听器 ---
    ll::event::EventBus::getInstance().emplaceListener<event::SetMoneyBeforeEvent>(
        [&logger](event::SetMoneyBeforeEvent& event) {
            logger.info("--- SetMoneyBeforeEvent 触发 ---");
            logger.info("  目标金额: {}", czmoney::MoneyManager::formatBalance(event.getAmount()));
            if (event.getReason1() == "TEST_MODIFY_SET") {
                event.getAmount() = 88888; // 888.88
                logger.warn("  [测试] 目标金额被修改为 888.88");
            }
            if (event.getReason1() == "TEST_CANCEL_SET") {
                event.cancel();
                logger.warn("  [测试] 设置金额事件被取消！");
            }
        }
    );
    ll::event::EventBus::getInstance().emplaceListener<event::SetMoneyAfterEvent>(
        [&logger](event::SetMoneyAfterEvent& event) {
            logger.info("--- SetMoneyAfterEvent 触发 ---");
            logger.info("  最终设置金额: {}", czmoney::MoneyManager::formatBalance(event.getAmount()));
            logger.info("------------------------------------");
        }
    );

    // --- SubtractMoneyEvent 监听器 ---
    ll::event::EventBus::getInstance().emplaceListener<event::SubtractMoneyBeforeEvent>(
        [&logger](event::SubtractMoneyBeforeEvent& event) {
            logger.info("--- SubtractMoneyBeforeEvent 触发 ---");
            logger.info("  计划减少金额: {}", czmoney::MoneyManager::formatBalance(event.getAmountToSubtract()));
            if (event.getReason1() == "TEST_MODIFY_SUB") {
                event.getAmountToSubtract() /= 2; // 减半
                logger.warn("  [测试] 减少的金额被减半！");
            }
            if (event.getReason1() == "TEST_CANCEL_SUB") {
                event.cancel();
                logger.warn("  [测试] 减少金额事件被取消！");
            }
        }
    );
    ll::event::EventBus::getInstance().emplaceListener<event::SubtractMoneyAfterEvent>(
        [&logger](event::SubtractMoneyAfterEvent& event) {
            logger.info("--- SubtractMoneyAfterEvent 触发 ---");
            logger.info("  实际减少金额: {}", czmoney::MoneyManager::formatBalance(event.getAmountToSubtract()));
            logger.info("------------------------------------");
        }
    );

    // --- TransferMoneyEvent 监听器 ---
    ll::event::EventBus::getInstance().emplaceListener<event::TransferMoneyBeforeEvent>(
        [&logger](event::TransferMoneyBeforeEvent& event) {
            logger.info("--- TransferMoneyBeforeEvent 触发 ---");
            logger.info("  发送方 UUID: {}", event.getSenderUuid());
            logger.info("  接收方 UUID: {}", event.getReceiverUuid());
            logger.info("  货币类型: {}", event.getCurrencyType());
            logger.info("  计划转账金额 (原始): {}", MoneyManager::formatBalance(event.getAmountToTransfer()));
            logger.info("  计划税费: {}", MoneyManager::formatBalance(event.getTaxAmount()));
            logger.info("  计划实际接收金额: {}", MoneyManager::formatBalance(event.getAmountReceived()));
            logger.info("  理由 1: '{}'", event.getReason1());
            logger.info("  理由 2: '{}'", event.getReason2());
            logger.info("  理由 3: '{}'", event.getReason3());

            // --- 测试逻辑：修改和取消 ---
            // 示例 1: 如果理由是 "TEST_TRANSFER_CANCEL"，则取消事件
            if (event.getReason1() == "TEST_TRANSFER_CANCEL") {
                event.cancel();
                logger.warn("  [测试] 转账事件已被取消！");
            }
            // 示例 2: 如果理由是 "TEST_TRANSFER_MODIFY_TAX"，则修改税费和接收金额
            if (event.getReason1() == "TEST_TRANSFER_MODIFY_TAX") {
                int64_t originalTax = event.getTaxAmount();
                event.getTaxAmount() = 0; // 免税
                event.getAmountReceived() = event.getAmountToTransfer(); // 接收全额
                logger.warn("  [测试] 税费从 {} 修改为 0，接收金额修改为全额 {}",
                            MoneyManager::formatBalance(originalTax),
                            MoneyManager::formatBalance(event.getAmountReceived()));
            }
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    ll::event::EventBus::getInstance().emplaceListener<event::TransferMoneyAfterEvent>(
        [&logger](event::TransferMoneyAfterEvent& event) {
            logger.info("--- TransferMoneyAfterEvent 触发 ---");
            logger.info("  发送方 UUID: {}", event.getSenderUuid());
            logger.info("  接收方 UUID: {}", event.getReceiverUuid());
            logger.info("  货币类型: {}", event.getCurrencyType());
            logger.info("  实际转账金额: {}", MoneyManager::formatBalance(event.getAmountToTransfer()));
            logger.info("  实际税费: {}", MoneyManager::formatBalance(event.getTaxAmount()));
            logger.info("  实际接收金额: {}", MoneyManager::formatBalance(event.getAmountReceived()));
            logger.info("  理由 1: '{}'", event.getReason1());
            logger.info("  理由 2: '{}'", event.getReason2());
            logger.info("  理由 3: '{}'", event.getReason3());
            logger.info("------------------------------------");
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    logger.info("czmoney 事件测试监听器注册完成。");
}
} // namespace czmoney::test
