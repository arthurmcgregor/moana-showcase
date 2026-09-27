#include <gtest/gtest.h>

#include "connectivity/connectivity_policy.h"

// F5: the idle-disconnect rule, lifted out of Connectivity::inactivityTask.
static constexpr unsigned long TIMEOUT_MS = 300000;

using ConnectivityPolicy::shouldDisconnectForInactivity;

TEST(ConnectivityPolicy, DisconnectsAfterTimeoutElapsed) {
  EXPECT_TRUE(shouldDisconnectForInactivity(1000, 1000 + TIMEOUT_MS, TIMEOUT_MS));
}

TEST(ConnectivityPolicy, DisconnectsAtExactlyTimeout) {
  // '>=' — reaching the timeout is enough.
  EXPECT_TRUE(shouldDisconnectForInactivity(1, 1 + TIMEOUT_MS, TIMEOUT_MS));
}

TEST(ConnectivityPolicy, StaysConnectedBeforeTimeout) {
  EXPECT_FALSE(
      shouldDisconnectForInactivity(1000, 1000 + TIMEOUT_MS - 1, TIMEOUT_MS));
}

TEST(ConnectivityPolicy, ZeroLastRequestNeverDisconnects) {
  // 0 means "no request ever seen" — must not be read as an ancient timestamp
  // that trips an immediate disconnect the moment the task first runs.
  EXPECT_FALSE(shouldDisconnectForInactivity(0, TIMEOUT_MS * 10, TIMEOUT_MS));
}
