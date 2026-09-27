#include <gtest/gtest.h>

#include "recording/batch_accumulator.h"
#include "hardware/gps_types.h"

class BatchAccumulatorTest : public ::testing::Test {
 protected:
  static constexpr size_t kCapacity = 10;
  static constexpr size_t kMinBatch = 3;
  GPSData buffer[kCapacity]{};
  BatchAccumulator<GPSData> acc{buffer, kCapacity, kMinBatch};

  GPSData makePoint(double lat, double lng) {
    GPSData d{};
    d.lat = lat;
    d.lng = lng;
    return d;
  }
};

TEST_F(BatchAccumulatorTest, StartsEmpty) {
  EXPECT_EQ(acc.count(), 0u);
  EXPECT_FALSE(acc.hasData());
  EXPECT_FALSE(acc.readyToFlush());
}

TEST_F(BatchAccumulatorTest, BatchNotReadyBelowThreshold) {
  EXPECT_FALSE(acc.add(makePoint(1.0, 2.0)));
  EXPECT_FALSE(acc.add(makePoint(3.0, 4.0)));
  EXPECT_EQ(acc.count(), 2u);
  EXPECT_TRUE(acc.hasData());
  EXPECT_FALSE(acc.readyToFlush());
}

TEST_F(BatchAccumulatorTest, BatchSignalsReadyAtThreshold) {
  acc.add(makePoint(1.0, 2.0));
  acc.add(makePoint(3.0, 4.0));
  EXPECT_TRUE(acc.add(makePoint(5.0, 6.0)));
  EXPECT_TRUE(acc.readyToFlush());
}

TEST_F(BatchAccumulatorTest, DataIsAccessibleAndMatchesInput) {
  acc.add(makePoint(-36.8, 174.7));
  acc.add(makePoint(-37.0, 175.0));

  const GPSData* data = acc.data();
  EXPECT_DOUBLE_EQ(data[0].lat, -36.8);
  EXPECT_DOUBLE_EQ(data[0].lng, 174.7);
  EXPECT_DOUBLE_EQ(data[1].lat, -37.0);
  EXPECT_DOUBLE_EQ(data[1].lng, 175.0);
}

TEST_F(BatchAccumulatorTest, ClearResetsForReuse) {
  acc.add(makePoint(1.0, 2.0));
  acc.add(makePoint(3.0, 4.0));
  acc.add(makePoint(5.0, 6.0));

  acc.clear();
  EXPECT_EQ(acc.count(), 0u);
  EXPECT_FALSE(acc.hasData());
  EXPECT_FALSE(acc.readyToFlush());
}

TEST_F(BatchAccumulatorTest, AddingBeyondCapacityIsSafe) {
  for (size_t i = 0; i < kCapacity + 5; i++) {
    acc.add(makePoint(static_cast<double>(i), 0.0));
  }
  EXPECT_EQ(acc.count(), kCapacity);
}

TEST_F(BatchAccumulatorTest, ForceAddBypassesThreshold) {
  acc.addNoThreshold(makePoint(1.0, 2.0));
  EXPECT_EQ(acc.count(), 1u);
  EXPECT_TRUE(acc.hasData());
  // Not ready to flush — addNoThreshold doesn't check threshold
  // but readyToFlush() still reports based on count vs minBatch
  EXPECT_FALSE(acc.readyToFlush());
}

TEST_F(BatchAccumulatorTest, ForceAddBeyondCapacityIsSafe) {
  for (size_t i = 0; i < kCapacity + 5; i++) {
    acc.addNoThreshold(makePoint(static_cast<double>(i), 0.0));
  }
  EXPECT_EQ(acc.count(), kCapacity);
}
