#pragma once

#include <cstdint>
#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>


namespace czmoney::event {

/**
 * @brief 玩家金额增加前事件 (可取消)
 *
 * 在玩家余额实际增加之前触发。
 * 监听器可以取消此事件以阻止金额增加，也可以修改将要增加的金额和操作理由。
 */
class AddMoneyBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
protected:
    std::string& mPlayerUuid;
    std::string& mCurrencyType;
    int64_t&     mAmountToAdd; // 整数形式 (实际金额 * 100)
    std::string& mReason1;
    std::string& mReason2;
    std::string& mReason3;

public:
    constexpr explicit AddMoneyBeforeEvent(
        std::string& playerUuid,
        std::string& currencyType,
        int64_t&     amountToAdd,
        std::string& reason1,
        std::string& reason2,
        std::string& reason3
    )
    : mPlayerUuid(playerUuid),
      mCurrencyType(currencyType),
      mAmountToAdd(amountToAdd),
      mReason1(reason1),
      mReason2(reason2),
      mReason3(reason3) {}

public:
    std::string& getPlayerUuid() const { return mPlayerUuid; }
    std::string& getCurrencyType() const { return mCurrencyType; }
    int64_t&     getAmountToAdd() const { return mAmountToAdd; }
    std::string& getReason1() const { return mReason1; }
    std::string& getReason2() const { return mReason2; }
    std::string& getReason3() const { return mReason3; }
};


/**
 * @brief 玩家金额增加后事件 (不可取消)
 *
 * 在玩家余额成功增加之后触发。
 * 监听器只能读取事件信息，不能修改。
 */
class AddMoneyAfterEvent final : public ll::event::Event {
protected:
    std::string const& mPlayerUuid;
    std::string const& mCurrencyType;
    int64_t const&     mAmountToAdd; // 整数形式 (实际金额 * 100)
    std::string const& mReason1;
    std::string const& mReason2;
    std::string const& mReason3;

public:
    constexpr explicit AddMoneyAfterEvent(
        std::string const& playerUuid,
        std::string const& currencyType,
        int64_t const&     amountToAdd,
        std::string const& reason1,
        std::string const& reason2,
        std::string const& reason3
    )
    : mPlayerUuid(playerUuid),
      mCurrencyType(currencyType),
      mAmountToAdd(amountToAdd),
      mReason1(reason1),
      mReason2(reason2),
      mReason3(reason3) {}

public:
    std::string const& getPlayerUuid() const;
    std::string const& getCurrencyType() const;
    int64_t const&     getAmountToAdd() const;
    std::string const& getReason1() const;
    std::string const& getReason2() const;
    std::string const& getReason3() const;
};

} // namespace czmoney::event