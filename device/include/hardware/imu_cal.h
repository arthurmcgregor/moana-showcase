#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

// Pure magnetometer calibration math — no hardware dependencies, so the same
// code is exercised by the native unit tests and the on-device fusion task.
//
// Bounding-box fit (per-axis min/max) is sufficient for the BMI270+BMM150 on
// our boat mount with residuals around 0.04 in a clean environment. If a
// future installation needs better, upgrade the fit() body to a 3×3 ellipsoid
// solve; the surrounding pipeline (sample collection, residual reporting,
// apply/discard UI) stays the same.
namespace ImuCal {

struct Sample {
  float x;
  float y;
  float z;
};

struct FitResult {
  float hardIron[3];   // bias offset to subtract before scaling
  float softIron[3];   // per-axis scale (avgRadius / axisRadius)
  float residualStd;   // stddev(r) / mean(r) — scale-invariant, target < 0.1
  bool valid;          // false if sample count < min or any axis collapsed
};

inline FitResult fit(const Sample* samples, size_t count, size_t minSamples = 200) {
  FitResult r{};
  if (count < minSamples) {
    r.valid = false;
    return r;
  }

  float xMin = samples[0].x, xMax = samples[0].x;
  float yMin = samples[0].y, yMax = samples[0].y;
  float zMin = samples[0].z, zMax = samples[0].z;
  for (size_t i = 1; i < count; ++i) {
    if (samples[i].x < xMin) xMin = samples[i].x;
    if (samples[i].x > xMax) xMax = samples[i].x;
    if (samples[i].y < yMin) yMin = samples[i].y;
    if (samples[i].y > yMax) yMax = samples[i].y;
    if (samples[i].z < zMin) zMin = samples[i].z;
    if (samples[i].z > zMax) zMax = samples[i].z;
  }

  float rX = (xMax - xMin) / 2.0f;
  float rY = (yMax - yMin) / 2.0f;
  float rZ = (zMax - zMin) / 2.0f;
  if (rX <= 0.0f || rY <= 0.0f || rZ <= 0.0f) {
    r.valid = false;
    return r;
  }

  float avgR = (rX + rY + rZ) / 3.0f;
  r.hardIron[0] = (xMax + xMin) / 2.0f;
  r.hardIron[1] = (yMax + yMin) / 2.0f;
  r.hardIron[2] = (zMax + zMin) / 2.0f;
  r.softIron[0] = avgR / rX;
  r.softIron[1] = avgR / rY;
  r.softIron[2] = avgR / rZ;

  // Residual: deviation of corrected sample radii from the mean radius.
  // Normalising by mean radius makes the metric scale-invariant so the
  // threshold (~0.04 clean, ~0.2 contaminated) is portable across mag units.
  double sumR = 0.0;
  double sumR2 = 0.0;
  for (size_t i = 0; i < count; ++i) {
    float cx = (samples[i].x - r.hardIron[0]) * r.softIron[0];
    float cy = (samples[i].y - r.hardIron[1]) * r.softIron[1];
    float cz = (samples[i].z - r.hardIron[2]) * r.softIron[2];
    double radius = std::sqrt((double)cx * cx + (double)cy * cy + (double)cz * cz);
    sumR += radius;
    sumR2 += radius * radius;
  }
  double meanR = sumR / (double)count;
  double varR = sumR2 / (double)count - meanR * meanR;
  if (varR < 0.0) varR = 0.0;
  r.residualStd = (float)(std::sqrt(varR) / meanR);
  r.valid = true;
  return r;
}

}  // namespace ImuCal
