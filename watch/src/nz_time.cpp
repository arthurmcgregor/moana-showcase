#include "nz_time.h"

#include "civil_time.h"

namespace nz_time {
namespace {

// Both New Zealand transitions happen at 02:00 *standard* time, which is the
// same instant in UTC either way: DST starts at 02:00 NZST (UTC+12) and ends
// at 03:00 NZDT (UTC+13), so both are 14:00 UTC on the Saturday before the
// nominal Sunday — 10 hours before that Sunday's UTC midnight.
constexpr int32_t kTransitionOffsetSeconds = -10 * 3600;

uint32_t transitionAt(int32_t days) {
  return (uint32_t)((int64_t)days * 86400 + kTransitionOffsetSeconds);
}

// DST starts on the last Sunday of September.
uint32_t dstStart(int32_t year) {
  const int32_t sept30 = civil_time::daysFromCivil(year, 9, 30);
  return transitionAt(sept30 - (int32_t)civil_time::weekdayFromDays(sept30));
}

// DST ends on the first Sunday of April.
uint32_t dstEnd(int32_t year) {
  const int32_t apr1 = civil_time::daysFromCivil(year, 4, 1);
  const uint32_t weekday = civil_time::weekdayFromDays(apr1);
  return transitionAt(apr1 + (int32_t)((7 - weekday) % 7));
}

}  // namespace

bool isDaylightTime(uint32_t utcSeconds) {
  // Southern hemisphere: the DST period spans the new year, so within any one
  // UTC year it is the two ends — before April's transition, or after
  // September's. Taking the year from the UTC instant rather than the local
  // one is safe: the two only disagree either side of UTC midnight on
  // 31 December / 1 January, and both of those fall inside DST regardless.
  const civil_time::Civil c = civil_time::fromEpoch(utcSeconds);
  return utcSeconds < dstEnd(c.year) || utcSeconds >= dstStart(c.year);
}

int32_t utcOffsetSeconds(uint32_t utcSeconds) {
  return isDaylightTime(utcSeconds) ? kNzdtOffsetSeconds : kNzstOffsetSeconds;
}

}  // namespace nz_time
