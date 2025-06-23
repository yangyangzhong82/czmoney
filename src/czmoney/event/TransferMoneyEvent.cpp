#include "czmoney/event/TransferMoneyEvent.h"
#include <ll/api/event/Emitter.h>

namespace czmoney::event {

// --- TransferMoneyBeforeEvent Getters ---
// (Getter bodies are defined in the header for constexpr, but if not, they would be here)

// --- Emitter for Before Event ---
class TransferMoneyBeforeEventEmitter : public ll::event::Emitter<[](auto&&...) { return nullptr; }, TransferMoneyBeforeEvent> {};


// --- TransferMoneyAfterEvent Getters ---
std::string const& TransferMoneyAfterEvent::getSenderUuid() const { return mSenderUuid; }
std::string const& TransferMoneyAfterEvent::getReceiverUuid() const { return mReceiverUuid; }
std::string const& TransferMoneyAfterEvent::getCurrencyType() const { return mCurrencyType; }
int64_t const&     TransferMoneyAfterEvent::getAmountToTransfer() const { return mAmountToTransfer; }
int64_t const&     TransferMoneyAfterEvent::getTaxAmount() const { return mTaxAmount; }
int64_t const&     TransferMoneyAfterEvent::getAmountReceived() const { return mAmountReceived; }
std::string const& TransferMoneyAfterEvent::getReason1() const { return mReason1; }
std::string const& TransferMoneyAfterEvent::getReason2() const { return mReason2; }
std::string const& TransferMoneyAfterEvent::getReason3() const { return mReason3; }

// --- Emitter for After Event ---
class TransferMoneyAfterEventEmitter : public ll::event::Emitter<[](auto&&...) { return nullptr; }, TransferMoneyAfterEvent> {};

} // namespace czmoney::event
