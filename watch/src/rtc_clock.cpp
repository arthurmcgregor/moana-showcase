#include "rtc_clock.h"

#include <LilyGoLib.h>

#include "civil_time.h"

namespace rtc_clock {

uint32_t RtcClock::readUtc() {
  // The PCF85063 raises its oscillator-stop bit whenever the rail behind the
  // RTC has been down, and clears it on the next write. So this asks exactly
  // the right question: has the chip been running continuously since someone
  // last set it? A "no" means the registers still hold digits, they just
  // aren't a time — which is the case #144 wants shown as `--:--` rather than
  // as a plausible-looking wrong hour.
  if (!instance.rtc.isClockIntegrityGuaranteed()) {
    return 0;
  }
  RTC_DateTime dt = instance.rtc.getDateTime();
  // The RTC holds UTC, never local time: the DST rule belongs at render time
  // (nz_time), and event timestamps go on the wire as UTC. Converting here
  // rather than through mktime keeps the C library's timezone state out of it.
  return civil_time::toEpoch(dt.getYear(), dt.getMonth(), dt.getDay(),
                             dt.getHour(), dt.getMinute(), dt.getSecond());
}

bool RtcClock::writeUtc(uint32_t utcSeconds) {
  civil_time::Civil c = civil_time::fromEpoch(utcSeconds);
  // SensorPCF85063::setDateTime returns void — the driver gives us no way to
  // tell a successful I2C write from a failed one. So ask the chip instead:
  // read the registers straight back. That also clears up the case the return
  // value could never have covered, an oscillator-stop bit still set because
  // the write never reached the device, which readUtc reports as 0.
  //
  // The alternative — returning an unverified true — is worse than it looks.
  // timebase only rewrites when telemetry disagrees with its own RAM estimate,
  // and the RAM estimate is rebased by that same packet, so it never disagrees
  // again: one lost write would go unnoticed until the next reboot showed
  // `--:--` (issue #144).
  instance.rtc.setDateTime(RTC_DateTime((uint16_t)c.year, c.month, c.day,
                                        c.hour, c.minute, c.second));

  uint32_t readBack = readUtc();
  if (readBack == 0) {
    return false;
  }
  uint32_t skew =
      readBack > utcSeconds ? readBack - utcSeconds : utcSeconds - readBack;
  // One second of tolerance: the RTC's own second can tick between the write
  // and the read, which is a clock that is working, not one that refused.
  return skew <= 1;
}

}  // namespace rtc_clock
