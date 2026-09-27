#include <gtest/gtest.h>

#include <vector>

#include "alerts.h"

using alerts::Cue;
using alerts::Fault;
using alerts::kGpsLostMs;
using alerts::kMaxRepeats;
using alerts::kRepeatMs;
using alerts::Monitor;

namespace {

constexpr uint32_t kTickMs = 250;  // the UI refresh period

// Everything healthy and recording, at nowMs.
alerts::Input ok(uint32_t nowMs) {
  alerts::Input in;
  in.nowMs = nowMs;
  in.armed = true;
  in.linkLost = false;
  in.fixValid = true;
  return in;
}

// Run the monitor forward to `untilMs` and return every non-NONE cue, so a
// test can assert on silence as easily as on a buzz.
std::vector<Cue> run(Monitor& m, alerts::Input in, uint32_t untilMs) {
  std::vector<Cue> cues;
  for (uint32_t t = in.nowMs; t <= untilMs; t += kTickMs) {
    in.nowMs = t;
    Cue c = m.tick(in);
    if (c != Cue::NONE) {
      cues.push_back(c);
    }
  }
  return cues;
}

// --- Arming: a watch that isn't recording says nothing ---

TEST(Alerts, SilentBeforeAnyTelemetry) {
  // Boot out of range: never received, so nothing is armed and the link has
  // always been "lost". Must not buzz.
  Monitor m;
  alerts::Input in;
  in.armed = false;
  in.linkLost = true;
  in.fixValid = false;
  EXPECT_TRUE(run(m, in, 10 * 60 * 1000).empty());
  EXPECT_EQ(m.fault(), Fault::NONE);
}

TEST(Alerts, SilentWhenNotRecording) {
  // Watch on the charger, boat switched off — permanently out of range.
  Monitor m;
  alerts::Input in = ok(1000);
  in.armed = false;
  in.linkLost = true;
  EXPECT_TRUE(run(m, in, 10 * 60 * 1000).empty());
}

// --- Link lost ---

TEST(Alerts, LinkLostBuzzesOnceOnEntry) {
  Monitor m;
  ASSERT_TRUE(run(m, ok(1000), 5000).empty());

  alerts::Input lost = ok(5250);
  lost.linkLost = true;
  EXPECT_EQ(m.tick(lost), Cue::LINK_LOST);
  EXPECT_EQ(m.fault(), Fault::LINK_LOST);

  // Nothing more until the repeat interval.
  EXPECT_TRUE(run(m, lost, 5250 + kRepeatMs - kTickMs).empty());
}

TEST(Alerts, LinkLostRepeatsThenGivesUp) {
  Monitor m;
  alerts::Input lost = ok(1000);
  lost.linkLost = true;
  ASSERT_EQ(m.tick(lost), Cue::LINK_LOST);

  // Long past the point where a capped reminder must have stopped.
  std::vector<Cue> cues = run(m, lost, 1000 + kRepeatMs * (kMaxRepeats + 5));
  EXPECT_EQ(cues.size(), kMaxRepeats);
  for (Cue c : cues) {
    EXPECT_EQ(c, Cue::LINK_LOST);
  }
}

TEST(Alerts, LinkRecoveryClearsOnce) {
  Monitor m;
  alerts::Input lost = ok(1000);
  lost.linkLost = true;
  ASSERT_EQ(m.tick(lost), Cue::LINK_LOST);

  EXPECT_EQ(m.tick(ok(9000)), Cue::CLEARED);
  EXPECT_EQ(m.fault(), Fault::NONE);
  EXPECT_TRUE(run(m, ok(9250), 9250 + kRepeatMs * 3).empty());
}

TEST(Alerts, RecordingStoppingClearsSilently) {
  Monitor m;
  alerts::Input lost = ok(1000);
  lost.linkLost = true;
  ASSERT_EQ(m.tick(lost), Cue::LINK_LOST);

  // The boat echoes IDLE: the fault didn't resolve, it stopped mattering.
  alerts::Input disarmed = lost;
  disarmed.nowMs = 2000;
  disarmed.armed = false;
  EXPECT_EQ(m.tick(disarmed), Cue::NONE);
  EXPECT_EQ(m.fault(), Fault::NONE);
}

TEST(Alerts, ReArmingWithTheFaultStillUpBuzzesAgain) {
  Monitor m;
  alerts::Input lost = ok(1000);
  lost.linkLost = true;
  ASSERT_EQ(m.tick(lost), Cue::LINK_LOST);

  alerts::Input disarmed = lost;
  disarmed.nowMs = 2000;
  disarmed.armed = false;
  ASSERT_EQ(m.tick(disarmed), Cue::NONE);

  lost.nowMs = 3000;
  EXPECT_EQ(m.tick(lost), Cue::LINK_LOST);
}

// --- GPS fix lost ---

TEST(Alerts, FixFlickerUnderTheDebounceIsSilent) {
  // The boat calls its own fix stale after 3 s without a GPS event, so a
  // marginal sky drops fix_valid repeatedly. None of that should buzz.
  Monitor m;
  alerts::Input noFix = ok(1000);
  noFix.fixValid = false;
  ASSERT_TRUE(run(m, noFix, 1000 + kGpsLostMs - kTickMs).empty());

  // Fix returns: the debounce restarts, so the next drop needs a full
  // kGpsLostMs of its own.
  ASSERT_EQ(m.tick(ok(1000 + kGpsLostMs)), Cue::NONE);
  noFix.nowMs = 1000 + kGpsLostMs + kTickMs;
  EXPECT_TRUE(run(m, noFix, noFix.nowMs + kGpsLostMs - kTickMs).empty());
  EXPECT_EQ(m.fault(), Fault::NONE);
}

TEST(Alerts, SustainedFixLossBuzzesAfterTheDebounce) {
  Monitor m;
  alerts::Input noFix = ok(1000);
  noFix.fixValid = false;
  std::vector<Cue> cues = run(m, noFix, 1000 + kGpsLostMs);
  ASSERT_EQ(cues.size(), 1u);
  EXPECT_EQ(cues[0], Cue::GPS_LOST);
  EXPECT_EQ(m.fault(), Fault::GPS_LOST);

  EXPECT_EQ(m.tick(ok(1000 + kGpsLostMs + kTickMs)), Cue::CLEARED);
}

TEST(Alerts, FixClockRunsWhileDisarmedSoRecordStartAlertsAtOnce) {
  // Hitting record under a sky that has been blank for a minute is worth
  // knowing about now, not in another ten seconds.
  Monitor m;
  alerts::Input idle = ok(1000);
  idle.armed = false;
  idle.fixValid = false;
  ASSERT_TRUE(run(m, idle, 61000).empty());

  alerts::Input recording = idle;
  recording.nowMs = 61250;
  recording.armed = true;
  EXPECT_EQ(m.tick(recording), Cue::GPS_LOST);
}

// --- The two faults together ---

TEST(Alerts, LinkLossOutranksFixLoss) {
  Monitor m;
  alerts::Input noFix = ok(1000);
  noFix.fixValid = false;
  ASSERT_EQ(run(m, noFix, 1000 + kGpsLostMs).size(), 1u);
  ASSERT_EQ(m.fault(), Fault::GPS_LOST);

  // Now the link goes too: the more fundamental fault takes over and says so.
  alerts::Input both = noFix;
  both.nowMs = 1000 + kGpsLostMs + kTickMs;
  both.linkLost = true;
  EXPECT_EQ(m.tick(both), Cue::LINK_LOST);
  EXPECT_EQ(m.fault(), Fault::LINK_LOST);
}

TEST(Alerts, LinkRecoveryWithoutAFixRestartsTheFixDebounce) {
  // Out of range the last fix_valid is frozen, so time spent with nobody
  // listening must not count towards the GPS alert — otherwise walking back
  // into range fires a fix alert on the strength of stale data.
  Monitor m;
  alerts::Input lost = ok(1000);
  lost.linkLost = true;
  lost.fixValid = false;
  ASSERT_EQ(m.tick(lost), Cue::LINK_LOST);
  ASSERT_TRUE(run(m, lost, 1000 + kGpsLostMs * 3).empty());

  // Link back, still no fix: LINK_LOST clears and the fix debounce starts
  // from here, so the fix alert lands kGpsLostMs later — not instantly.
  uint32_t back = 1000 + kGpsLostMs * 3 + kTickMs;
  alerts::Input noFix = lost;
  noFix.nowMs = back;
  noFix.linkLost = false;
  EXPECT_EQ(m.tick(noFix), Cue::CLEARED);
  EXPECT_TRUE(run(m, noFix, back + kGpsLostMs - kTickMs).empty());

  std::vector<Cue> cues = run(m, noFix, back + kGpsLostMs + kTickMs);
  ASSERT_EQ(cues.size(), 1u);
  EXPECT_EQ(cues[0], Cue::GPS_LOST);
}

}  // namespace
