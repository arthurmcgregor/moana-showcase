#pragma once

#include <Adafruit_AHRS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <array>
#include <atomic>
#include <functional>

#include "hardware/imu_cal.h"
#include "hardware/imu_types.h"

class App;

// On-device IMU subsystem: BMI270 + BMM150 fused via Mahony at 100 Hz.
// Mounted on the starboard side of the centreboard case (see remapToBoat).
//
// Modes:
//   FUSING              — normal fused operation feeding live atomics + accum
//   CALIBRATING         — fusion paused; mag samples collected for offline fit
//   CAL_RESULT_PENDING  — fusion paused; result waits for explicit apply/discard
//
// The bench-test handoff is explicit that calibration is in-situ and should
// not happen mid-record. Caller (AppController) must check RecordingState ==
// IDLE before calling startCalibration().
enum class IMUMode : uint8_t {
  FUSING,
  CALIBRATING,
  CAL_RESULT_PENDING,
};

// Snapshot of in-progress calibration. Read coherently under the IMU mux.
struct CalProgress {
  IMUMode mode;
  uint32_t elapsedMs;
  uint32_t samples;
  float xMin, xMax, yMin, yMax, zMin, zMax;
};

// Result of a completed calibration window. valid=false means the bounding
// box collapsed on an axis (no rotation about that axis) — caller should
// discard and recalibrate; never apply.
struct CalResult {
  float hardIron[3];
  float softIron[3];
  float residualStd;
  uint32_t sampleCount;
  bool tooFewSamples;
  bool valid;
};

class IMU {
 public:
  IMU();
  void init();

  // Gate for the 100 Hz fusion loop (QW8). When set and it returns false while
  // FUSING, the loop skips the sensor read + Mahony update — cutting ~100 I²C
  // transactions/s while nothing consumes the output (device idle and asleep).
  // Wired by App to "recording active OR display awake". Calibration is never
  // gated. Unset (the default) means always run, preserving prior behaviour.
  void setFusionGate(std::function<bool()> gate) { _fusionGate = std::move(gate); }

  // Live (non-draining) snapshot for ESP-NOW telemetry. Values freeze when
  // the fusion gate pauses the loop (device idle + display asleep) — stale
  // heel/trim is acceptable for the watch link, so telemetry does not hold
  // the gate open.
  IMUSnapshot getLatest() const;
  IMUSnapshot drainAccumulator();

  IMUMode getMode() const { return _mode.load(std::memory_order_acquire); }

  // True only if the IMU hardware was detected in init(). The caller uses this
  // to skip spawning the fusion task entirely — otherwise the task runs with no
  // sensor, sets valid=true, and stamps garbage heel/trim/heading into the CSV
  // (B9).
  bool isPresent() const { return _present; }

  // Returns false if a calibration is already in progress (FUSING is the
  // only valid starting state). Caller must enforce RecordingState == IDLE.
  bool startCalibration();
  // Cancels a CALIBRATING or CAL_RESULT_PENDING; mag offsets unchanged.
  void cancelCalibration();
  // Persists the pending result to NVS, reseeds the filter, returns to
  // FUSING. Returns false if no valid pending result exists.
  bool applyPendingCalibration();
  // Drops the pending result; returns to FUSING with old offsets retained.
  void discardPendingCalibration();

  CalProgress getCalProgress() const;
  CalResult getPendingResult() const;

  static void fusionTask(void* pvParameters);

 private:
  void runFusionLoop();
  void tick();
  void seedInitialAlignment();
  void loadMagCal();
  void saveMagCal();
  void calibrationTick();
  void finishCalibration();
  static void remapToBoat(float dx, float dy, float dz, float& bx, float& by, float& bz);

  Adafruit_Mahony _filter;

  float _hardIron[3];
  float _softIron[3];

  float _heel = 0.0f;
  float _trim = 0.0f;
  float _heading = 0.0f;
  bool _present = false;  // set in init() on hardware detection; read once at spawn
  std::atomic<bool> _initialised{false};
  std::function<bool()> _fusionGate;  // QW8 idle gate; empty => always run

  struct Accum {
    double heel = 0.0;
    double trim = 0.0;
    double headingX = 0.0;
    double headingY = 0.0;
    uint32_t n = 0;
    unsigned long startMs = 0;
  };
  Accum _accum;

  // Guards live values, accum, and the calibration progress/result fields
  // so drainAccumulator()/getCalProgress() each see a coherent snapshot.
  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  std::atomic<IMUMode> _mode{IMUMode::FUSING};

  // Mag sample buffer for the bounding-box fit + residual computation.
  // 90 s × ~25 Hz mag ODR ≈ 2250 samples; round up for safety. ~28 KB SRAM,
  // always allocated to avoid heap fragmentation.
  static constexpr size_t MAX_CAL_SAMPLES = 2400;
  std::array<ImuCal::Sample, MAX_CAL_SAMPLES> _calSamples{};
  uint32_t _calSampleCount = 0;
  unsigned long _calStartMs = 0;
  float _calXMin = 0, _calXMax = 0, _calYMin = 0, _calYMax = 0, _calZMin = 0, _calZMax = 0;
  CalResult _pendingResult{};

  static constexpr float SAMPLE_RATE_HZ = 100.0f;
  static constexpr float MAHONY_KP = 5.0f;
  static constexpr float MAHONY_KI = 0.0f;
  static constexpr unsigned long MAX_WINDOW_MS = 2000;
  static constexpr unsigned long CAL_DURATION_MS = 90000;
  static constexpr uint32_t MIN_CAL_SAMPLES = 200;
};
