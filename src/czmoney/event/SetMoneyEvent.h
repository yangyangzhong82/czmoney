#pragma once

#include <cstdint>
#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>


namespace czmoney::event {

/**
 * @brief 玩家金额设置前事件 (可取消)
 */
class SetMoneyBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
protected:
    std::string& mPlayerUuid;
    std::string& mCurrencyType;
    int64_t&     mAmount; // 目标金额
    std::string& mReason1;
    std::string& mReason2;
    std::string& mReason3;

public:
    constexpr explicit SetMoneyBeforeEvent(
        std::string& playerUuid,
        std::string& currencyType,
        int64_t&     amount,
        std::string& reason1,
        std::string& reason2,
        std::string& reason3
    )
    : mPlayerUuid(playerUuid),
      mCurrencyType(currencyType),
      mAmount(amount),
      mReason1(reason1),
      mReason2(reason2),
      mReason3(reason3) {}

public:
    std::string& getPlayerUuid() const { return mPlayerUuid; }
    std::string& getCurrencyType() const { return mCurrencyType; }
    int64_t&     getAmount() const { return mAmount; }
    std::string& getReason1() const { return mReason1; }
    std::string& getReason2() const { return mReason2; }
    std::string& getReason3() const { return mReason3; }
};

/**
 * @brief 玩家金额设置后事件 (不可取消)
 */
class SetMoneyAfterEvent final : public ll::event::Event {
protected:
    std::string const& mPlayerUuid;
    std::string const& mCurrencyType;
    int64_t const&     mAmount;
    std::string const& mReason1;
    std::string const& mReason2;
    std::string const& mReason3;

public:
    constexpr explicit SetMoneyAfterEvent(
        std::string const& playerUuid,
        std::string const& currencyType,
        int64_t const&     amount,
        std::string const& reason1,
        std::string const& reason2,
        std::string const& reason3
    )
    : mPlayerUuid(playerUuid),
      mCurrencyType(currencyType),
      mAmount(amount),
      mReason1(reason1),
      mReason2(reason2),
      mReason3(reason3) {}

public:
    std::string const& getPlayerUuid() const;
    std::string const& getCurrencyType() const;
    int64_t const&     getAmount() const;
    std::string const& getReason1() const;
    std::string const& getReason2() const;
    std::string const& getReason3() const;
};

} // namespace czmoney::event