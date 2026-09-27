#include <gtest/gtest.h>

#include <cmath>

#include "recording/distance_accumulator.h"

// Deterministic planar distance so expectations are exact: 1 unit of
// lat/lng = 1 metre. The real great-circle maths belongs to TinyGPSPlus
// (injected by App); the accumulator's contract is only about WHEN it
// integrates, never how far two points are apart.
static double planar(double lat1, double lng1, double lat2, double lng2) {
  double dLat = lat2 - lat1;
  double dLng = lng2 - lng1;
  return std::sqrt(dLat * dLat + dLng * dLng);
}

class DistanceAccumulatorTest : public ::testing::Test {
 protected:
  DistanceAccumulator acc;
  void SetUp() override { acc.init(planar); }
};

TEST_F(DistanceAccumulatorTest, StartsAtZero) {
  EXPECT_EQ(acc.totalMeters(), 0u);
}

TEST_F(DistanceAccumulatorTest, AccumulatesAcrossValidFixesWhileActive) {
  acc.onFix(0, 0, true);   // anchor only — nothing to integrate from yet
  acc.onFix(0, 3, true);   // +3
  acc.onFix(4, 3, true);   // +4
  EXPECT_EQ(acc.totalMeters(), 7u);
}

TEST_F(DistanceAccumulatorTest, DoesNotAccumulateWhileNotRecording) {
  acc.onFix(0, 0, false);
  acc.onFix(0, 100, false);
  EXPECT_EQ(acc.totalMeters(), 0u);
}

TEST_F(DistanceAccumulatorTest, ResetsOnRecordingStart) {
  // First session: 5 m.
  acc.onFix(0, 0, true);
  acc.onFix(0, 5, true);
  EXPECT_EQ(acc.totalMeters(), 5u);

  // Stop, drift around, restart: total restarts from 0 and the pre-start
  // position never contributes to the new session's first segment.
  acc.onFix(0, 50, false);
  acc.onFix(0, 60, true);  // first ACTIVE fix of session 2 — reset + anchor
  EXPECT_EQ(acc.totalMeters(), 0u);
  acc.onFix(0, 62, true);  // +2
  EXPECT_EQ(acc.totalMeters(), 2u);
}

TEST_F(DistanceAccumulatorTest, HoldsValueAfterStop) {
  acc.onFix(0, 0, true);
  acc.onFix(0, 8, true);
  EXPECT_EQ(acc.totalMeters(), 8u);

  // Stopped: the finished session's distance stays on the echo, and further
  // movement never adds to it.
  acc.onFix(0, 100, false);
  acc.onFix(0, 200, false);
  EXPECT_EQ(acc.totalMeters(), 8u);
}

TEST_F(DistanceAccumulatorTest, NoAccumulationAcrossInvalidGap) {
  acc.onFix(0, 0, true);
  acc.onFix(0, 3, true);  // +3
  EXPECT_EQ(acc.totalMeters(), 3u);

  // Fixes go invalid, boat travels 100 m blind: the jump across the gap is
  // NOT integrated — the next valid fix only re-anchors.
  acc.onFixLost();
  acc.onFix(0, 103, true);
  EXPECT_EQ(acc.totalMeters(), 3u);

  // Accumulation resumes from that new anchor.
  acc.onFix(0, 105, true);  // +2
  EXPECT_EQ(acc.totalMeters(), 5u);
}

TEST_F(DistanceAccumulatorTest, FixLostWhileIdleIsHarmless) {
  acc.onFixLost();
  acc.onFix(0, 0, true);
  acc.onFix(0, 4, true);
  EXPECT_EQ(acc.totalMeters(), 4u);
}

TEST_F(DistanceAccumulatorTest, RoundsFractionalMetresAtPublish) {
  acc.onFix(0, 0, true);
  acc.onFix(0, 0.6, true);
  EXPECT_EQ(acc.totalMeters(), 1u);  // 0.6 -> rounds to 1
  acc.onFix(0, 1.2, true);
  EXPECT_EQ(acc.totalMeters(), 1u);  // 1.2 total -> still 1
  acc.onFix(0, 1.6, true);
  EXPECT_EQ(acc.totalMeters(), 2u);  // 1.6 total -> 2
}

TEST_F(DistanceAccumulatorTest, NoDistanceFnMeansNoAccumulation) {
  // Defensive: an unwired accumulator must not crash the GPS fan-out.
  DistanceAccumulator bare;
  bare.onFix(0, 0, true);
  bare.onFix(0, 5, true);
  EXPECT_EQ(bare.totalMeters(), 0u);
}
