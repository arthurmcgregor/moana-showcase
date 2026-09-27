#pragma once

#include <cstdint>

#include "hardware/gps_types.h"
#include "hardware/imu_types.h"

// One CSV row's worth of data, captured at GPS-fix arrival time. IMU is
// snapshotted then (not at file-write time) so each row reflects the IMU
// window between this fix and the previous fix, regardless of how long the
// row sits in the batch queue before flush.
//
// Kept free of FreeRTOS/Arduino includes so RecordingSession (and its tests)
// can consume it in the native build.
struct RecordedSample {
  GPSData gps;
  IMUSnapshot imu;
  // millis() at the moment the fix arrived, stamped in Recorder::feedGPSData.
  // The CSV timestamp column comes from here, NOT from millis() at flush time —
  // otherwise every row in a batch would share one flush-time value up to
  // ~10-30 s after the fix, flattening downstream time-series (wind/VMG).
  uint32_t capturedAtMs = 0;
};
