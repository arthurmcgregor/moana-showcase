#include <gtest/gtest.h>

#include "civil_time.h"
#include "nz_time.h"

namespace {

// Readable fixtures: a UTC instant from its calendar fields.
uint32_t utc(int32_t y, uint32_t mo, uint32_t d, uint32_t h, uint32_t mi = 0) {
  return civil_time::toEpoch(y, mo, d, h, mi, 0);
}

// What the Speed tile actually renders, so a test failure reads like the watch.
int localHour(uint32_t utcSeconds) {
  uint32_t local = utcSeconds + (uint32_t)nz_time::utcOffsetSeconds(utcSeconds);
  return (int)(local / 3600 % 24);
}

TEST(CivilTime, RoundTripsTheEpochAndAKnownDate) {
  civil_time::Civil c = civil_time::fromEpoch(1753900000);
  EXPECT_EQ(c.year, 2025);
  EXPECT_EQ(c.month, 7);
  EXPECT_EQ(c.day, 30);
  EXPECT_EQ(c.hour, 18);
  EXPECT_EQ(c.minute, 26);
  EXPECT_EQ(c.second, 40);
  EXPECT_EQ(civil_time::toEpoch(2025, 7, 30, 18, 26, 40), 1753900000u);
}

TEST(CivilTime, HandlesTheLeapDayTheGregorianRuleKeeps) {
  // 2000 is a leap year (divisible by 400), 1900 was not — the case a naive
  // "%4" calendar gets wrong, and the reason this isn't a hand-rolled table.
  EXPECT_EQ(civil_time::fromEpoch(civil_time::toEpoch(2000, 2, 29, 0, 0, 0)).day,
            29);
  EXPECT_EQ(civil_time::toEpoch(2024, 3, 1, 0, 0, 0) -
                civil_time::toEpoch(2024, 2, 28, 0, 0, 0),
            2u * 86400u);
}

TEST(CivilTime, KnowsTheWeekdayOfTheEpochAndAKnownSunday) {
  EXPECT_EQ(civil_time::weekdayFromDays(0), 4u);  // 1970-01-01 was a Thursday
  EXPECT_EQ(civil_time::weekdayFromDays(civil_time::daysFromCivil(2026, 9, 27)),
            0u);  // last Sunday of September 2026
}

// --- the rule itself ---

TEST(NzTime, StandardTimeThroughTheSouthernWinter) {
  EXPECT_FALSE(nz_time::isDaylightTime(utc(2026, 6, 15, 0)));
  EXPECT_EQ(nz_time::utcOffsetSeconds(utc(2026, 6, 15, 0)),
            nz_time::kNzstOffsetSeconds);
}

TEST(NzTime, DaylightTimeThroughTheSouthernSummer) {
  EXPECT_TRUE(nz_time::isDaylightTime(utc(2026, 12, 15, 0)));
  EXPECT_EQ(nz_time::utcOffsetSeconds(utc(2026, 12, 15, 0)),
            nz_time::kNzdtOffsetSeconds);
}

TEST(NzTime, SwitchesOnTheLastSundayOfSeptember) {
  // 2026-09-27 02:00 NZST == 2026-09-26 14:00 UTC. The season this bug was
  // filed against: 23 days after it was found, the fixed +12 goes wrong.
  const uint32_t transition = utc(2026, 9, 26, 14);
  EXPECT_FALSE(nz_time::isDaylightTime(transition - 1));
  EXPECT_TRUE(nz_time::isDaylightTime(transition));
  // The hour the wearer sees skips 2am straight to 3am.
  EXPECT_EQ(localHour(transition - 1), 1);
  EXPECT_EQ(localHour(transition), 3);
}

TEST(NzTime, SwitchesBackOnTheFirstSundayOfApril) {
  // 2026-04-05 03:00 NZDT == 2026-04-04 14:00 UTC.
  const uint32_t transition = utc(2026, 4, 4, 14);
  EXPECT_TRUE(nz_time::isDaylightTime(transition - 1));
  EXPECT_FALSE(nz_time::isDaylightTime(transition));
  // 3am repeats as 2am — the wearer sees the hour again, not a jump.
  EXPECT_EQ(localHour(transition - 1), 2);
  EXPECT_EQ(localHour(transition), 2);
}

TEST(NzTime, HoldsAcrossTheNewYearWhereUtcAndLocalDisagreeOnTheYear) {
  // 2025-12-31T23:00Z is already 2026-01-01 noon in NZ. The rule reads the
  // year off the UTC instant, so this is the case that would break if the
  // two ends of the DST period were tested against different years.
  EXPECT_TRUE(nz_time::isDaylightTime(utc(2025, 12, 31, 23)));
  EXPECT_TRUE(nz_time::isDaylightTime(utc(2026, 1, 1, 0, 30)));
  EXPECT_EQ(localHour(utc(2025, 12, 31, 23)), 12);
}

TEST(NzTime, TransitionDatesMoveWithTheCalendarNotWithAFixedDay) {
  // Different day-of-month each year: 2025 switches on the 28th, 2026 on the
  // 27th, 2027 on the 26th. A hardcoded date would pass one year and fail two.
  EXPECT_TRUE(nz_time::isDaylightTime(utc(2025, 9, 27, 14)));
  EXPECT_FALSE(nz_time::isDaylightTime(utc(2025, 9, 26, 14)));
  EXPECT_TRUE(nz_time::isDaylightTime(utc(2027, 9, 25, 14)));
  EXPECT_FALSE(nz_time::isDaylightTime(utc(2027, 9, 24, 14)));
}

TEST(NzTime, SeptemberThirtiethIsAlwaysDaylightTime) {
  // The last Sunday can fall as early as the 24th and as late as the 30th;
  // the month's end is inside the period either way.
  for (int32_t year = 2024; year <= 2030; year++) {
    EXPECT_TRUE(nz_time::isDaylightTime(utc(year, 9, 30, 23)))
        << "September 30 " << year;
    EXPECT_FALSE(nz_time::isDaylightTime(utc(year, 9, 1, 0)))
        << "September 1 " << year;
  }
}

}  // namespace
