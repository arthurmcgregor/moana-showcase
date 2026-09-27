#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "hardware/imu_cal.h"

using ImuCal::Sample;

namespace {

// Build a point cloud on the ellipsoid of half-axes (rx, ry, rz) centred at
// (ox, oy, oz). Always seeds the six axis extrema so the bounding-box fit
// gets a clean target; remaining samples fill the surface.
std::vector<Sample> ellipsoidPoints(float rx, float ry, float rz,
                                    float ox, float oy, float oz, int n) {
  std::vector<Sample> v;
  v.reserve(n);
  v.push_back({ox + rx, oy, oz});
  v.push_back({ox - rx, oy, oz});
  v.push_back({ox, oy + ry, oz});
  v.push_back({ox, oy - ry, oz});
  v.push_back({ox, oy, oz + rz});
  v.push_back({ox, oy, oz - rz});
  for (int i = 6; i < n; ++i) {
    float u = (float)i / (float)n;
    float phi = u * (float)M_PI;
    float theta = (float)i * 2.39996f;  // golden-angle spiral, no clumping
    v.push_back({ox + rx * std::sin(phi) * std::cos(theta),
                 oy + ry * std::sin(phi) * std::sin(theta),
                 oz + rz * std::cos(phi)});
  }
  return v;
}

}  // namespace

TEST(ImuCalFit, RejectsTooFewSamples) {
  Sample s[10] = {};
  auto r = ImuCal::fit(s, 10);
  EXPECT_FALSE(r.valid);
}

TEST(ImuCalFit, RejectsDegenerateAxis) {
  // All samples on a plane — z range is zero.
  std::vector<Sample> samples;
  for (int i = 0; i < 300; ++i) {
    samples.push_back({(float)(i % 17), (float)(i % 13), 0.0f});
  }
  auto r = ImuCal::fit(samples.data(), samples.size());
  EXPECT_FALSE(r.valid);
}

TEST(ImuCalFit, RecoversHardIronFromOffsetSphere) {
  auto samples = ellipsoidPoints(100.0f, 100.0f, 100.0f, -50.0f, 30.0f, 70.0f, 1000);
  auto r = ImuCal::fit(samples.data(), samples.size());

  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.hardIron[0], -50.0f, 0.01f);
  EXPECT_NEAR(r.hardIron[1], 30.0f, 0.01f);
  EXPECT_NEAR(r.hardIron[2], 70.0f, 0.01f);
  EXPECT_NEAR(r.softIron[0], 1.0f, 0.001f);
  EXPECT_NEAR(r.softIron[1], 1.0f, 0.001f);
  EXPECT_NEAR(r.softIron[2], 1.0f, 0.001f);
  EXPECT_LT(r.residualStd, 0.01f);
}

TEST(ImuCalFit, RecoversSoftIronFromEllipsoid) {
  // 100 / 120 / 80 per-axis radii — soft-iron factors normalise to avgR = 100.
  auto samples = ellipsoidPoints(100.0f, 120.0f, 80.0f, 0.0f, 0.0f, 0.0f, 1000);
  auto r = ImuCal::fit(samples.data(), samples.size());

  ASSERT_TRUE(r.valid);
  float avgR = (100.0f + 120.0f + 80.0f) / 3.0f;
  EXPECT_NEAR(r.softIron[0], avgR / 100.0f, 0.01f);
  EXPECT_NEAR(r.softIron[1], avgR / 120.0f, 0.01f);
  EXPECT_NEAR(r.softIron[2], avgR / 80.0f, 0.01f);
}

TEST(ImuCalFit, ResidualGrowsWithNoise) {
  // Noisy data should always have a worse (larger) residual than clean data
  // taken from the same surface. The bounding-box fit is sensitive to which
  // samples happen to land at the extrema, so the absolute residual depends
  // on the noise pattern — but the clean < noisy ordering is invariant.
  auto clean = ellipsoidPoints(100.0f, 100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 1000);
  auto noisy = clean;
  for (size_t i = 6; i < noisy.size(); ++i) {
    // Leave the 6 axis extrema untouched so hardIron recovery stays clean;
    // perturb only the interior surface points.
    float jitter = 0.95f + (float)((i * 73) % 11) / 100.0f;  // 0.95..1.05
    noisy[i].x *= jitter;
    noisy[i].y *= jitter;
    noisy[i].z *= jitter;
  }
  auto rClean = ImuCal::fit(clean.data(), clean.size());
  auto rNoisy = ImuCal::fit(noisy.data(), noisy.size());

  ASSERT_TRUE(rClean.valid);
  ASSERT_TRUE(rNoisy.valid);
  EXPECT_LT(rClean.residualStd, 0.01f);
  EXPECT_GT(rNoisy.residualStd, rClean.residualStd);
  EXPECT_LT(rNoisy.residualStd, 0.10f);
}
