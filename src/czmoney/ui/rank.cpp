#include "czmoney/ui/rank.h"
#include "czmoney/MyMod.h"
// #include "czmoney/money/money.h" // 不再直接需要 MoneyManager，改为使用 API
#include "ll/api/form/SimpleForm.h"
#include "mc/world/actor/player/Player.h" // 修正为正确的 Player 头文件
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h" // 修正为正确的 UUID 头文件
#include <fmt/format.h>
#include <string>
#include <vector>
#include <algorithm> // For std::sort (if needed, though DB handles sorting)
#include "czmoney/logger.h" // 用于日志记录
#include "czmoney/money/money_api.h" // 引入 money_api.h

// 将所有实现放在命名空间内
namespace czmoney::ui {

RankForm::RankForm(Player& player)
    : ll::form::SimpleForm("金币排行榜"), // 修正基类初始化
          mPlayer(player)
    {
        // 移除 MoneyManager 实例的直接获取，改为直接调用 API
        // czmoney::MoneyManager* moneyManager = nullptr;
        // try {
        //     moneyManager = &czmoney::MyMod::getInstance().getMoneyManager();
        // } catch (const std::exception& e) {
        //     logger.error("无法获取 MoneyManager 实例: {}", e.what());
        //     mPlayer.sendMessage("§c无法获取经济系统数据，请联系管理员。");
        //     return;
        // }

    // 获取配置中的默认货币类型
    const auto& config = czmoney::MyMod::getInstance().getConfig();
    std::string defaultCurrencyType = "money"; // 默认值
    if (config.economy.count("money")) { // 检查 "money" 是否存在于配置中
        defaultCurrencyType = "money"; // 确保使用配置中存在的货币类型
    } else if (!config.economy.empty()) {
        defaultCurrencyType = config.economy.begin()->first; // 如果 "money" 不存在，使用第一个定义的货币类型
    } else {
        logger.warn("配置中未找到任何货币类型，将使用默认 'money'。");
    }

    // 获取排行榜数据，通过 API 调用
    // 假设我们总是显示前10名
    std::vector<std::pair<std::string, int64_t>> topBalances =
        czmoney::api::getTopBalances(defaultCurrencyType, 10, 0); // 调用 API

    // 构建表单内容
    std::string content = "";
    if (topBalances.empty()) {
        content = "§l§e暂无金币排行榜数据。";
    } else {
        for (size_t i = 0; i < topBalances.size(); ++i) {
            const auto& entry = topBalances[i];
            std::string uuid = entry.first;
            int64_t balance = entry.second;

            std::string playerName = getPlayerName(uuid);
            std::string formattedBalance = czmoney::api::formatBalance(balance); // 调用 API

            // 根据排名设置颜色
            std::string rankColor = "§f"; // 默认白色
            if (i == 0) {
                rankColor = "§6"; // 第1名：金色
            } else if (i == 1) {
                rankColor = "§e"; // 第2名：黄色
            } else if (i == 2) {
                rankColor = "§a"; // 第3名：绿色
            } else {
                rankColor = "§f"; // 其他名次：白色
            }

            content += fmt::format("{}{}第{}名: {} - {}{}金币\n",
                                   rankColor,
                                   (i < 3 ? "§l" : ""), // 前三名加粗
                                   i + 1,
                                   playerName,
                                   formattedBalance,
                                   (i < 3 ? "§r" : "")); // 恢复颜色和粗体
        }
    }

    // 设置表单内容
    setContent(content);

    // 添加一个按钮 (图片中有一个“提交”按钮，虽然这里没有实际功能，但为了还原UI)
    appendButton("提交"); // 修正 addButton 为 appendButton

    // 发送表单，并直接传入回调函数
    sendTo(mPlayer, [this](Player& player, int selected, ll::form::FormCancelReason reason) { // 修正回调函数签名
        if (selected == -1) { // 玩家关闭表单
            logger.debug("排行榜表单被玩家 {} 关闭。", player.getRealName());
        } else {
            // 如果有多个按钮，这里可以根据按钮索引处理点击事件
            logger.debug("排行榜表单被玩家 {} 提交，点击了按钮索引: {}", player.getRealName(), selected);
        }
    });
}

// getPlayerName 方法的定义应该在 RankForm 类外部，但仍在 czmoney::ui 命名空间内
std::string RankForm::getPlayerName(const std::string& uuid) {
    // 使用 mce::UUID::fromString 转换字符串 UUID
    auto playerInfoOpt = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuid)); // 修正命名空间
    if (playerInfoOpt.has_value()) {
        return playerInfoOpt.value().name; // 返回玩家名称
    }
    logger.warn("无法获取 UUID {} 的玩家名称，将显示为 '未知玩家'。", uuid);
    return "未知玩家";
}

} // namespace czmoney::ui
