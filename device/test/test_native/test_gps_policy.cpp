#include <gtest/gtest.h>

#include "hardware/gps_policy.h"

// F5: the LOCKED->LOST staleness rule, lifted out of GPS::healthTask.
static constexpr unsigned long STALE_MS = 10000;

TEST(GpsPolicy, LockedGoesLostWhenFixOlderThanThreshold) {
  EXPECT_EQ(GpsPolicy::healthCheck(GPSState::LOCKED, STALE_MS + 1, STALE_MS),
            GPSState::LOST);
}

TEST(GpsPolicy, LockedStaysLockedAtExactlyThreshold) {
  // Strict '>' — a fix exactly at the threshold age is not yet stale.
  EXPECT_EQ(GpsPolicy::healthCheck(GPSState::LOCKED, STALE_MS, STALE_MS),
            GPSState::LOCKED);
}

TEST(GpsPolicy, LockedStaysLockedWhenFixFresh) {
  EXPECT_EQ(GpsPolicy::healthCheck(GPSState::LOCKED, 0, STALE_MS),
            GPSState::LOCKED);
}

// The health check only decays a LOCKED fix; every other state is inert here
// (reacquisition to LOCKED happens on the fresh-data path, not this check).
TEST(GpsPolicy, NonLockedStatesUnchangedEvenWhenStale) {
  const unsigned long ancient = STALE_MS * 100;
  EXPECT_EQ(GpsPolicy::healthCheck(GPSState::ACQUIRING, ancient, STALE_MS),
            GPSState::ACQUIRING);
  EXPECT_EQ(GpsPolicy::healthCheck(GPSState::LOST, ancient, STALE_MS),
            GPSState::LOST);
  EXPECT_EQ(GpsPolicy::healthCheck(GPSState::NOT_STARTED, ancient, STALE_MS),
            GPSState::NOT_STARTED);
}
