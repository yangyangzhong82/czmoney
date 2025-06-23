#include "czmoney/event/AddMoneyEvent.h"
#include <ll/api/event/Emitter.h>

namespace czmoney::event {

// --- AddMoneyBeforeEvent Getters ---
// (Getter bodies are defined in the header for constexpr, but if not, they would be here)

// --- Emitter for Before Event ---
class AddMoneyBeforeEventEmitter : public ll::event::Emitter<[](auto&&...) { return nullptr; }, AddMoneyBeforeEvent> {};


// --- AddMoneyAfterEvent Getters ---
std::string const& AddMoneyAfterEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string const& AddMoneyAfterEvent::getCurrencyType() const { return mCurrencyType; }
int64_t const&     AddMoneyAfterEvent::getAmountToAdd() const { return mAmountToAdd; }
std::string const& AddMoneyAfterEvent::getReason1() const { return mReason1; }
std::string const& AddMoneyAfterEvent::getReason2() const { return mReason2; }
std::string const& AddMoneyAfterEvent::getReason3() const { return mReason3; }

// --- Emitter for After Event ---
class AddMoneyAfterEventEmitter : public ll::event::Emitter<[](auto&&...) { return nullptr; }, AddMoneyAfterEvent> {};

} // namespace czmoney::event
