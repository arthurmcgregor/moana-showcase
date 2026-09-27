#include <gtest/gtest.h>

#include "stats_engine.h"

using stats_engine::Engine;
using stats_engine::LegView;
using stats_engine::SessionView;

namespace {

// Drives the engine like ui.cpp does: one packet per simulated second, with
// the boat echo fields held in fixture state so tests read as scenarios.
class StatsEngineTest : public ::testing::Test {
 protected:
  Engine eng;
  uint32_t now = 10'000;
  uint32_t cum = 0;
  uint8_t recState = moana::RECORDING_IDLE;
  uint8_t mode = moana::MODE_SAIL;
  uint16_t ack = moana::kEventSeqNone;

  // Sends the current echo state as a fresh packet at `now`.
  void packet(float sog = 5.0f, bool fixValid = true, float hdop = 1.0f) {
    Engine::Input in;
    in.nowMs = now;
    in.packet = true;
    in.sogKnots = sog;
    in.fixValid = fixValid;
    in.hdop = hdop;
    in.cumulativeDistanceM = cum;
    in.recordingState = recState;
    in.currentMode = mode;
    in.eventSeqAcked = ack;
    eng.tick(in);
  }

  // 1 Hz cadence: advance one second, then send.
  void second(float sog = 5.0f, bool fixValid = true, float hdop = 1.0f) {
    now += 1000;
    packet(sog, fixValid, hdop);
  }

  // A tick with no fresh packet (link gap / between packets).
  void idleTick(uint32_t advanceMs) {
    now += advanceMs;
    Engine::Input in;
    in.nowMs = now;
    in.packet = false;
    eng.tick(in);
  }

  // Observed recording start: one idle packet, then the recording echo.
  void startRecording() {
    packet();
    recState = moana::RECORDING_ACTIVE;
    second();
  }

  // Cuts are debounced (issue #161): a leg younger than kMinLegMs is
  // relabelled instead of cut, so a test that wants a real cut has to let the
  // current leg age past the window first. Advances the clock only — no packet,
  // so nothing lands in the speed pipeline.
  void ageCurrentLeg() { now += stats_engine::kMinLegMs; }

  LegView currentLeg() { return eng.leg(eng.legCount() - 1); }
};

// --- session lifecycle ---

TEST_F(StatsEngineTest, NoSessionBeforeRecording) {
  packet();
  second();
  EXPECT_FALSE(eng.session().exists);
  EXPECT_FALSE(eng.sessionLive());
  EXPECT_EQ(eng.legCount(), 0u);
}

TEST_F(StatsEngineTest, SessionStartsOnRecordingEcho) {
  startRecording();
  EXPECT_TRUE(eng.sessionLive());
  ASSERT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().number, 1);
  EXPECT_EQ(currentLeg().mode, moana::MODE_SAIL);
}

TEST_F(StatsEngineTest, StartingStateAlreadyCountsAsRecording) {
  packet();
  recState = moana::RECORDING_STARTING;
  second();
  EXPECT_TRUE(eng.sessionLive());
}

TEST_F(StatsEngineTest, AdoptsInProgressSessionWithZeroBaseline) {
  // The very first packet the watch ever hears already shows recording:
  // watch rebooted mid-sail. The cumulative echo restores the distance.
  recState = moana::RECORDING_ACTIVE;
  cum = 5000;
  packet();
  EXPECT_TRUE(eng.sessionLive());
  EXPECT_EQ(eng.session().distanceM, 5000u);
}

TEST_F(StatsEngineTest, ObservedStartBaselinesOnEchoAndRebases) {
  // While idle the echo still holds the PREVIOUS session's total.
  cum = 5000;
  packet();
  // STARTING can be echoed before the device resets its accumulator — the
  // baseline lands on the stale 5000...
  recState = moana::RECORDING_STARTING;
  second();
  EXPECT_EQ(eng.session().distanceM, 0u);
  // ...and when the device resets to ~0 the baseline slides down instead of
  // underflowing.
  recState = moana::RECORDING_ACTIVE;
  cum = 2;
  second();
  EXPECT_EQ(eng.session().distanceM, 0u);
  cum = 42;
  second();
  EXPECT_EQ(eng.session().distanceM, 40u);
}

TEST_F(StatsEngineTest, StopFreezesStatsNotClears) {
  startRecording();
  cum = 1000;
  second();

  recState = moana::RECORDING_STOPPING;
  second();
  EXPECT_FALSE(eng.sessionLive());
  EXPECT_TRUE(eng.session().exists);
  uint32_t distAtStop = eng.session().distanceM;
  uint32_t elapsedAtStop = eng.session().elapsedMs;
  EXPECT_EQ(distAtStop, 1000u);

  // Time moves on; the frozen session doesn't.
  idleTick(60'000);
  EXPECT_EQ(eng.session().distanceM, distAtStop);
  EXPECT_EQ(eng.session().elapsedMs, elapsedAtStop);
  EXPECT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().distanceM, distAtStop);
}

TEST_F(StatsEngineTest, RestartResetsSessionAndLegs) {
  startRecording();
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  cum = 500;
  second();
  ASSERT_EQ(eng.legCount(), 2u);

  recState = moana::RECORDING_IDLE;
  second();
  recState = moana::RECORDING_ACTIVE;
  second();
  EXPECT_TRUE(eng.sessionLive());
  EXPECT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().number, 1);
  EXPECT_EQ(eng.session().distanceM, 0u);
}

TEST_F(StatsEngineTest, ElapsedKeepsCountingWithoutPackets) {
  startRecording();
  uint32_t before = eng.session().elapsedMs;
  idleTick(30'000);  // out-of-range gap: time keeps counting
  EXPECT_EQ(eng.session().elapsedMs, before + 30'000);
  EXPECT_TRUE(eng.sessionLive());
}

// --- distance snapshots ---

TEST_F(StatsEngineTest, LegAndSessionDistancesAreCumulativeDiffs) {
  startRecording();
  cum = 100;
  second();
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;  // cut
  second();
  cum = 150;
  second();

  ASSERT_EQ(eng.legCount(), 2u);
  EXPECT_EQ(eng.leg(0).distanceM, 100u);  // frozen at the cut
  EXPECT_EQ(eng.leg(1).distanceM, 50u);
  EXPECT_EQ(eng.session().distanceM, 150u);
}

TEST_F(StatsEngineTest, AvgIsDistanceOverElapsed) {
  startRecording();
  // 100 seconds, 1000 m -> 10 m/s = 19.44 kn.
  now += 100'000;
  cum = 1000;
  packet();
  EXPECT_NEAR(eng.session().avgKn, 19.44f, 0.05f);
  EXPECT_NEAR(currentLeg().avgKn, 19.44f, 0.05f);
}

// --- leg cutting ---

TEST_F(StatsEngineTest, ModeChangeCutsLeg) {
  startRecording();
  second();
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  second();

  ASSERT_EQ(eng.legCount(), 2u);
  EXPECT_EQ(eng.leg(0).mode, moana::MODE_SAIL);
  EXPECT_EQ(eng.leg(1).mode, moana::MODE_MOTOR);
  EXPECT_EQ(eng.leg(1).number, 2);
}

TEST_F(StatsEngineTest, FirstClassificationLabelsLegWithoutCutting) {
  // Boat just booted: mode UNKNOWN when recording starts. The first mode
  // event classifies the running leg — it is not a new leg.
  mode = moana::MODE_UNKNOWN;
  startRecording();
  ASSERT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().mode, moana::MODE_UNKNOWN);

  mode = moana::MODE_SAIL;
  ack = 1;  // the mode event that classified it
  second();
  EXPECT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().mode, moana::MODE_SAIL);
}

TEST_F(StatsEngineTest, SameModeReassertCutsViaAckAdvance) {
  // PWR-click manual cut: the event re-asserts the current mode, so the
  // echo's mode never changes — only the ack frontier advances.
  ack = 5;
  startRecording();
  second();
  ASSERT_EQ(eng.legCount(), 1u);

  ageCurrentLeg();
  ack = 6;
  second();
  ASSERT_EQ(eng.legCount(), 2u);
  EXPECT_EQ(eng.leg(0).mode, moana::MODE_SAIL);
  EXPECT_EQ(eng.leg(1).mode, moana::MODE_SAIL);
}

TEST_F(StatsEngineTest, AckAdvanceInSessionStartPacketDoesNotCut) {
  // A mode press racing the recording start: both land in the same packet.
  ack = 5;
  packet();
  recState = moana::RECORDING_ACTIVE;
  ack = 6;
  second();
  EXPECT_EQ(eng.legCount(), 1u);
}

TEST_F(StatsEngineTest, AckFromNoneDoesNotCut) {
  // Boat reboot resets its frontier to none; the first ack after that is
  // event delivery, not a manual cut.
  ack = moana::kEventSeqNone;
  startRecording();
  second();
  ack = 1;
  second();
  EXPECT_EQ(eng.legCount(), 1u);
}

TEST_F(StatsEngineTest, ModeChangeWithSimultaneousAckCutsOnce) {
  ack = 5;
  startRecording();
  second();
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  ack = 6;
  second();
  EXPECT_EQ(eng.legCount(), 2u);
}

TEST_F(StatsEngineTest, LegRingIsBoundedButNumberingContinues) {
  startRecording();
  for (int i = 0; i < 40; i++) {
    ageCurrentLeg();  // each leg has to outlive the debounce window to cut
    mode = (mode == moana::MODE_SAIL) ? moana::MODE_MOTOR : moana::MODE_SAIL;
    second();
  }
  EXPECT_EQ(eng.legCount(), stats_engine::kMaxLegs);
  EXPECT_EQ(currentLeg().number, 41);  // leg 1 + 40 cuts
  EXPECT_EQ(eng.leg(0).number, 41 - (int)stats_engine::kMaxLegs + 1);
}

// --- mis-press debounce (issue #161) ---

TEST_F(StatsEngineTest, MisPressWithinWindowRelabelsInsteadOfCutting) {
  // The case from the issue: tapped SAIL, meant MOTOR, tapped again. One leg,
  // now labelled MOTOR, still leg 1, still started when the session did.
  startRecording();
  uint32_t startedElapsed = currentLeg().elapsedMs;
  second();
  mode = moana::MODE_MOTOR;
  second();

  ASSERT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().mode, moana::MODE_MOTOR);
  EXPECT_EQ(currentLeg().number, 1);
  EXPECT_EQ(currentLeg().elapsedMs, startedElapsed + 2000);
}

TEST_F(StatsEngineTest, ChainOfMisPressesCollapsesToOneLeg) {
  startRecording();
  mode = moana::MODE_MOTORSAIL;
  second();
  second();
  mode = moana::MODE_MOTOR;
  second();

  ASSERT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().mode, moana::MODE_MOTOR);
}

TEST_F(StatsEngineTest, RelabelKeepsTheLegsTrackers) {
  // Same water, same samples: a relabel must not zero the leg's MAX the way a
  // fresh leg would.
  startRecording();
  for (int i = 0; i < 6; i++) {
    second(6.0f);
  }
  ASSERT_FLOAT_EQ(currentLeg().maxKn, 6.0f);
  cum = 400;

  mode = moana::MODE_MOTOR;
  second(6.0f);
  EXPECT_EQ(eng.legCount(), 1u);
  EXPECT_FLOAT_EQ(currentLeg().maxKn, 6.0f);
  EXPECT_EQ(currentLeg().distanceM, 400u);
}

TEST_F(StatsEngineTest, MisPressAfterARealCutRelabelsThatLegOnly) {
  startRecording();
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  second();
  ASSERT_EQ(eng.legCount(), 2u);

  // Immediately corrected: leg 2 becomes ANCHOR, leg 1 is untouched.
  mode = moana::MODE_ANCHOR;
  second();
  ASSERT_EQ(eng.legCount(), 2u);
  EXPECT_EQ(eng.leg(0).mode, moana::MODE_SAIL);
  EXPECT_EQ(eng.leg(1).mode, moana::MODE_ANCHOR);
  EXPECT_EQ(eng.leg(1).number, 2);
}

TEST_F(StatsEngineTest, SameModeReassertWithinWindowIsSwallowed) {
  // A PWR-click manual cut seconds after the leg opened is the same noise as a
  // mis-press — the server collapses it identically.
  ack = 5;
  startRecording();
  second();
  ack = 6;
  second();

  EXPECT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().mode, moana::MODE_SAIL);
}

TEST_F(StatsEngineTest, WindowIsMeasuredFromTheLegStartNotTheLastPress) {
  // A drip of presses just inside the window must not chain forever: the
  // relabel leaves the leg's start where it was, so the next press is judged
  // against that, not against the press before it.
  startRecording();
  now += stats_engine::kMinLegMs - 2000;
  mode = moana::MODE_MOTOR;
  packet();
  ASSERT_EQ(eng.legCount(), 1u);

  now += 4000;  // still 2 s short of a window since the LAST press
  mode = moana::MODE_ANCHOR;
  packet();
  ASSERT_EQ(eng.legCount(), 2u);
  EXPECT_EQ(eng.leg(0).mode, moana::MODE_MOTOR);
  EXPECT_EQ(eng.leg(1).mode, moana::MODE_ANCHOR);
}

// --- manual reset ---

TEST_F(StatsEngineTest, ManualResetRestartsLiveSession) {
  startRecording();
  cum = 800;
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  second();
  ASSERT_EQ(eng.legCount(), 2u);

  ASSERT_TRUE(eng.resetSession());
  EXPECT_TRUE(eng.sessionLive());
  EXPECT_EQ(eng.legCount(), 1u);
  EXPECT_EQ(currentLeg().number, 1);
  EXPECT_EQ(currentLeg().mode, moana::MODE_MOTOR);
  EXPECT_EQ(eng.session().distanceM, 0u);

  cum = 900;
  second();
  EXPECT_EQ(eng.session().distanceM, 100u);
}

TEST_F(StatsEngineTest, ManualResetRejectedWhileNotRecording) {
  packet();
  EXPECT_FALSE(eng.resetSession());

  startRecording();
  recState = moana::RECORDING_IDLE;
  second();
  EXPECT_FALSE(eng.resetSession());  // frozen stats stay browsable
}

// --- speed pipeline: gate ---

TEST_F(StatsEngineTest, InvalidFixSamplesAreIgnored) {
  startRecording();
  for (int i = 0; i < 10; i++) {
    second(5.0f);
  }
  float maxBefore = eng.session().maxKn;
  second(25.0f, /*fixValid=*/false);
  EXPECT_FLOAT_EQ(eng.session().maxKn, maxBefore);
}

TEST_F(StatsEngineTest, HighHdopSamplesAreIgnored) {
  startRecording();
  for (int i = 0; i < 10; i++) {
    second(5.0f);
  }
  float maxBefore = eng.session().maxKn;
  second(25.0f, true, /*hdop=*/4.0f);
  EXPECT_FLOAT_EQ(eng.session().maxKn, maxBefore);
}

TEST_F(StatsEngineTest, ImpossibleAccelerationSpikeDoesNotMoveMaxOr10s) {
  startRecording();
  for (int i = 0; i < 15; i++) {
    second(5.0f);
  }
  ASSERT_TRUE(eng.session().hasBest10s);
  float maxBefore = eng.session().maxKn;
  float bestBefore = eng.session().best10sKn;

  // A yacht can't gain 15 kn in one second: gated out entirely.
  second(20.0f);
  EXPECT_FLOAT_EQ(eng.session().maxKn, maxBefore);
  EXPECT_FLOAT_EQ(eng.session().best10sKn, bestBefore);

  // And the stream recovers immediately at the real speed.
  second(5.2f);
  EXPECT_TRUE(eng.sessionLive());
}

TEST_F(StatsEngineTest, GateResyncsAfterSustainedChange) {
  startRecording();
  for (int i = 0; i < 6; i++) {
    second(5.0f);
  }
  // Genuine step to 12 kn (say a tow): every 12 kn sample is rejected at
  // first, but the allowed delta grows with the time since the last
  // accepted sample, so the gate re-syncs instead of locking out forever.
  bool resynced = false;
  for (int i = 0; i < 10; i++) {
    second(12.0f);
    if (eng.session().maxKn > 10.0f) {
      resynced = true;
      break;
    }
  }
  EXPECT_TRUE(resynced);
}

// --- speed pipeline: median ---

TEST_F(StatsEngineTest, MedianFiltersGatePassingSpike) {
  startRecording();
  for (int i = 0; i < 8; i++) {
    second(5.0f);
  }
  // +1.5 kn passes the accel gate but the median-of-5 swallows the isolated
  // sample: MAX must not move.
  second(6.5f);
  second(5.0f);
  second(5.0f);
  EXPECT_FLOAT_EQ(eng.session().maxKn, 5.0f);
}

TEST_F(StatsEngineTest, SustainedSpeedRaisesMax) {
  startRecording();
  for (int i = 0; i < 6; i++) {
    second(5.0f);
  }
  for (int i = 0; i < 6; i++) {
    second(6.5f);
  }
  EXPECT_FLOAT_EQ(eng.session().maxKn, 6.5f);
}

// --- speed pipeline: rolling 10 s window ---

TEST_F(StatsEngineTest, NoBest10sInFirstTenSeconds) {
  startRecording();
  for (int i = 0; i < 5; i++) {
    second(5.0f);
  }
  EXPECT_FALSE(eng.session().hasBest10s);
  EXPECT_FALSE(currentLeg().hasBest10s);
  for (int i = 0; i < 10; i++) {
    second(5.0f);
  }
  EXPECT_TRUE(eng.session().hasBest10s);
  EXPECT_NEAR(eng.session().best10sKn, 5.0f, 0.01f);
}

TEST_F(StatsEngineTest, WindowAcrossTelemetryGapYieldsNoCandidate) {
  startRecording();
  for (int i = 0; i < 15; i++) {
    second(5.0f);
  }
  ASSERT_TRUE(eng.session().hasBest10s);
  float bestBefore = eng.session().best10sKn;

  // 5 s telemetry gap, then faster samples: any window spanning the gap
  // must yield no candidate — best-10s holds until a clean 10 s exists.
  now += 5000;
  for (int i = 0; i < 8; i++) {
    second(6.0f);
  }
  EXPECT_FLOAT_EQ(eng.session().best10sKn, bestBefore);

  // Once 10 contiguous seconds at 6 kn exist, the best rises honestly.
  for (int i = 0; i < 8; i++) {
    second(6.0f);
  }
  EXPECT_GT(eng.session().best10sKn, 5.5f);
}

TEST_F(StatsEngineTest, NewLegHasNoBest10sUntilFullWindowInsideIt) {
  startRecording();
  for (int i = 0; i < 20; i++) {
    second(5.0f);
  }
  ASSERT_TRUE(currentLeg().hasBest10s);

  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  second(5.0f);
  // Fresh leg: session best-10s persists, the leg's own is empty until a
  // whole window fits inside the leg.
  EXPECT_TRUE(eng.session().hasBest10s);
  EXPECT_FALSE(currentLeg().hasBest10s);
  for (int i = 0; i < 8; i++) {
    second(5.0f);
  }
  EXPECT_FALSE(currentLeg().hasBest10s);
  for (int i = 0; i < 5; i++) {
    second(5.0f);
  }
  EXPECT_TRUE(currentLeg().hasBest10s);
}

TEST_F(StatsEngineTest, MaxIsPerLegAndPerSession) {
  startRecording();
  for (int i = 0; i < 8; i++) {
    second(6.0f);
  }
  ageCurrentLeg();
  mode = moana::MODE_MOTOR;
  second(6.0f);
  for (int i = 0; i < 8; i++) {
    second(4.0f);
  }
  // Session max remembers the fast sail leg; the motor leg's own max is
  // lower. (The median window straddling the cut still carries 6 kn values
  // briefly, so the leg max settles at 6 for the first samples — assert
  // ranges, not exact equality, where the straddle applies.)
  EXPECT_FLOAT_EQ(eng.session().maxKn, 6.0f);
  ASSERT_EQ(eng.legCount(), 2u);
  EXPECT_FLOAT_EQ(eng.leg(0).maxKn, 6.0f);
  EXPECT_LE(eng.leg(1).maxKn, 6.0f);
}

}  // namespace
