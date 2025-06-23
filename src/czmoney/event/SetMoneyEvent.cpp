#include "czmoney/event/SetMoneyEvent.h"
#include <ll/api/event/Emitter.h>

namespace czmoney::event {

class SetMoneyBeforeEventEmitter : public ll::event::Emitter<[](auto&&...) { return nullptr; }, SetMoneyBeforeEvent> {};

std::string const& SetMoneyAfterEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string const& SetMoneyAfterEvent::getCurrencyType() const { return mCurrencyType; }
int64_t const&     SetMoneyAfterEvent::getAmount() const { return mAmount; }
std::string const& SetMoneyAfterEvent::getReason1() const { return mReason1; }
std::string const& SetMoneyAfterEvent::getReason2() const { return mReason2; }
std::string const& SetMoneyAfterEvent::getReason3() const { return mReason3; }

class SetMoneyAfterEventEmitter : public ll::event::Emitter<[](auto&&...) { return nullptr; }, SetMoneyAfterEvent> {};

} // namespace czmoney::event