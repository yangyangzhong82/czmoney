#pragma once

#include <cstdint>
#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>


namespace czmoney::event {

/**
 * @brief 玩家转账前事件 (可取消)
 *
 * 在玩家余额实际转账之前触发。
 * 监听器可以取消此事件以阻止转账，也可以修改将要转账的金额和税费。
 */
class TransferMoneyBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
protected:
    std::string& mSenderUuid;
    std::string& mReceiverUuid;
    std::string& mCurrencyType;
    int64_t&     mAmountToTransfer; // 整数形式 (实际金额 * 100)
    int64_t&     mTaxAmount;        // 税费 (整数形式 * 100)
    int64_t&     mAmountReceived;   // 实际接收金额 (整数形式 * 100)
    std::string& mReason1;
    std::string& mReason2;
    std::string& mReason3;

public:
    constexpr explicit TransferMoneyBeforeEvent(
        std::string& senderUuid,
        std::string& receiverUuid,
        std::string& currencyType,
        int64_t&     amountToTransfer,
        int64_t&     taxAmount,
        int64_t&     amountReceived,
        std::string& reason1,
        std::string& reason2,
        std::string& reason3
    )
    : mSenderUuid(senderUuid),
      mReceiverUuid(receiverUuid),
      mCurrencyType(currencyType),
      mAmountToTransfer(amountToTransfer),
      mTaxAmount(taxAmount),
      mAmountReceived(amountReceived),
      mReason1(reason1),
      mReason2(reason2),
      mReason3(reason3) {}

public:
    std::string& getSenderUuid() const { return mSenderUuid; }
    std::string& getReceiverUuid() const { return mReceiverUuid; }
    std::string& getCurrencyType() const { return mCurrencyType; }
    int64_t&     getAmountToTransfer() const { return mAmountToTransfer; }
    int64_t&     getTaxAmount() const { return mTaxAmount; }
    int64_t&     getAmountReceived() const { return mAmountReceived; }
    std::string& getReason1() const { return mReason1; }
    std::string& getReason2() const { return mReason2; }
    std::string& getReason3() const { return mReason3; }
};

/**
 * @brief 玩家转账后事件 (不可取消)
 *
 * 在玩家余额成功转账之后触发。
 * 监听器只能读取事件信息，不能修改。
 */
class TransferMoneyAfterEvent final : public ll::event::Event {
protected:
    std::string const& mSenderUuid;
    std::string const& mReceiverUuid;
    std::string const& mCurrencyType;
    int64_t const&     mAmountToTransfer; // 整数形式 (实际金额 * 100)
    int64_t const&     mTaxAmount;        // 税费 (整数形式 * 100)
    int64_t const&     mAmountReceived;   // 实际接收金额 (整数形式 * 100)
    std::string const& mReason1;
    std::string const& mReason2;
    std::string const& mReason3;

public:
    constexpr explicit TransferMoneyAfterEvent(
        std::string const& senderUuid,
        std::string const& receiverUuid,
        std::string const& currencyType,
        int64_t const&     amountToTransfer,
        int64_t const&     taxAmount,
        int64_t const&     amountReceived,
        std::string const& reason1,
        std::string const& reason2,
        std::string const& reason3
    )
    : mSenderUuid(senderUuid),
      mReceiverUuid(receiverUuid),
      mCurrencyType(currencyType),
      mAmountToTransfer(amountToTransfer),
      mTaxAmount(taxAmount),
      mAmountReceived(amountReceived),
      mReason1(reason1),
      mReason2(reason2),
      mReason3(reason3) {}

public:
    std::string const& getSenderUuid() const;
    std::string const& getReceiverUuid() const;
    std::string const& getCurrencyType() const;
    int64_t const&     getAmountToTransfer() const;
    int64_t const&     getTaxAmount() const;
    int64_t const&     getAmountReceived() const;
    std::string const& getReason1() const;
    std::string const& getReason2() const;
    std::string const& getReason3() const;
};

} // namespace czmoney::event
