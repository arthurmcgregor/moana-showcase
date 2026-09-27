// Proleptic-Gregorian calendar arithmetic on Unix seconds (issues #144/#175).
//
// The watch has to turn a UTC epoch into a date twice over: the RTC stores
// broken-down fields rather than an epoch, and the NZ daylight-saving rule is
// expressed in calendar terms ("last Sunday of September"). Neither can use
// gmtime/mktime — those live behind the C library's timezone state, which on
// this firmware is whatever the ESP-IDF happens to default to, and the native
// test build would inherit the host's instead.
//
// Howard Hinnant's days_from_civil / civil_from_days, which are exact for the
// whole int32 day range and involve no lookup tables. Pure and header-only:
// no Arduino, no LVGL, no allocation.
#pragma once

#include <stdint.h>

namespace civil_time {

struct Civil {
  int32_t year;
  uint8_t month;   // 1-12
  uint8_t day;     // 1-31
  uint8_t hour;    // 0-23
  uint8_t minute;  // 0-59
  uint8_t second;  // 0-59
};

// Days since 1970-01-01 for a proleptic-Gregorian date. Negative before 1970.
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= (m <= 2);
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);                    // [0, 399]
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // [0, 146096]
  return era * 146097 + (int32_t)doe - 719468;
}

// Inverse of daysFromCivil: the date that many days after 1970-01-01.
inline void civilFromDays(int32_t z, int32_t* y, uint32_t* m, uint32_t* d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);                 // [0, 146096]
  const uint32_t yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;         // [0, 399]
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);      // [0, 365]
  const uint32_t mp = (5 * doy + 2) / 153;                           // [0, 11]
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;                 // [1, 31]
  const uint32_t month = mp + (mp < 10 ? 3 : -9);                    // [1, 12]
  *y = (int32_t)yoe + era * 400 + (int32_t)(month <= 2);
  *m = month;
  *d = day;
}

// Day of the week for a day count, 0 = Sunday. Day 0 (1970-01-01) was a
// Thursday, hence the +4; the extra +7 keeps the modulo positive before 1970.
inline uint32_t weekdayFromDays(int32_t days) {
  return (uint32_t)(((days % 7) + 11) % 7);
}

inline Civil fromEpoch(uint32_t utcSeconds) {
  const int32_t days = (int32_t)(utcSeconds / 86400u);
  const uint32_t rem = utcSeconds % 86400u;
  Civil c{};
  int32_t y;
  uint32_t m, d;
  civilFromDays(days, &y, &m, &d);
  c.year = y;
  c.month = (uint8_t)m;
  c.day = (uint8_t)d;
  c.hour = (uint8_t)(rem / 3600u);
  c.minute = (uint8_t)(rem / 60u % 60u);
  c.second = (uint8_t)(rem % 60u);
  return c;
}

inline uint32_t toEpoch(int32_t year, uint32_t month, uint32_t day,
                        uint32_t hour, uint32_t minute, uint32_t second) {
  const int32_t days = daysFromCivil(year, month, day);
  return (uint32_t)days * 86400u + hour * 3600u + minute * 60u + second;
}

}  // namespace civil_time
