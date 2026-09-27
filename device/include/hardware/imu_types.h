#pragma once

// Pure data types for IMU — no hardware dependencies.
// Mirrors the gps_types.h split: types used by CSV / tests live here so they
// can be built in the native env without pulling in Arduino or M5Unified.

struct IMUSnapshot {
  float heel;     // deg, + to starboard
  float trim;     // deg, + bow up
  float heading;  // deg, 0–360 magnetic
  bool valid;     // false if no IMU samples have arrived yet
};
