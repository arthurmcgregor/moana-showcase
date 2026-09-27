#pragma once

#include "timebase.h"

// Production timebase::Clock on the T-Watch Ultra's PCF85063 (issue #144).
// The counterpart of event_store_nvs for the event queue: all the hardware
// this firmware's timekeeping touches, in one adapter the native tests never
// compile.
namespace rtc_clock {

class RtcClock : public timebase::Clock {
 public:
  uint32_t readUtc() override;
  bool writeUtc(uint32_t utcSeconds) override;
};

}  // namespace rtc_clock
