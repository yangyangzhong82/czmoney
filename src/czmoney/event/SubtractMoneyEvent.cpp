#include "czmoney/event/SubtractMoneyEvent.h"
#include <ll/api/event/Emitter.h>

namespace czmoney::event {

class SubtractMoneyBeforeEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, SubtractMoneyBeforeEvent> {};

std::string const& SubtractMoneyAfterEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string const& SubtractMoneyAfterEvent::getCurrencyType() const { return mCurrencyType; }
int64_t const&     SubtractMoneyAfterEvent::getAmountToSubtract() const { return mAmountToSubtract; }
std::string const& SubtractMoneyAfterEvent::getReason1() const { return mReason1; }
std::string const& SubtractMoneyAfterEvent::getReason2() const { return mReason2; }
std::string const& SubtractMoneyAfterEvent::getReason3() const { return mReason3; }

class SubtractMoneyAfterEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, SubtractMoneyAfterEvent> {};

} // namespace czmoney::event