#include "hardware/imu.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>

#include "app.h"
#include "utils/logger.h"

static const char* LOG_TAG = "IMU";

// NVS namespace + keys: shared with device/imu_sketch.cpp so a bench-run
// calibration carries over to production firmware without re-cal.
static constexpr const char* PREF_NAMESPACE = "magcal";

// Park-clean defaults from the bench test (issue #66 handoff). Used only
// when NVS has no calibration written. Recalibrate in situ on the boat.
static constexpr float DEFAULT_HARD_IRON[3] = {-387.072f, -108.231f, -198.554f};
static constexpr float DEFAULT_SOFT_IRON[3] = {0.9830f, 0.9989f, 1.0188f};

IMU::IMU() {
  _hardIron[0] = DEFAULT_HARD_IRON[0];
  _hardIron[1] = DEFAULT_HARD_IRON[1];
  _hardIron[2] = DEFAULT_HARD_IRON[2];
  _softIron[0] = DEFAULT_SOFT_IRON[0];
  _softIron[1] = DEFAULT_SOFT_IRON[1];
  _softIron[2] = DEFAULT_SOFT_IRON[2];
}

void IMU::init() {
  if (!M5.Imu.isEnabled()) {
    LOG_ERROR(LOG_TAG, "IMU not detected — fusion task will not be spawned");
    return;
  }
  _present = true;

  // Disable M5Unified's on-the-fly auto-calibration; would conflict with
  // our explicit hard-iron offsets.
  M5.Imu.setCalibration(0, 0, 0);
  M5.Imu.loadOffsetFromNVS();

  loadMagCal();

  _filter.begin(SAMPLE_RATE_HZ);
  _filter.setKp(MAHONY_KP);
  _filter.setKi(MAHONY_KI);
}

void IMU::loadMagCal() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE, true);
  if (prefs.isKey("hi0")) {
    _hardIron[0] = prefs.getFloat("hi0");
    _hardIron[1] = prefs.getFloat("hi1");
    _hardIron[2] = prefs.getFloat("hi2");
    _softIron[0] = prefs.getFloat("si0");
    _softIron[1] = prefs.getFloat("si1");
    _softIron[2] = prefs.getFloat("si2");
    LOG_INFO(LOG_TAG, "Loaded mag calibration from NVS");
  } else {
    LOG_WARN(LOG_TAG, "No mag calibration in NVS — using park-clean defaults");
  }
  prefs.end();
}

void IMU::saveMagCal() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE, false);
  prefs.putFloat("hi0", _hardIron[0]);
  prefs.putFloat("hi1", _hardIron[1]);
  prefs.putFloat("hi2", _hardIron[2]);
  prefs.putFloat("si0", _softIron[0]);
  prefs.putFloat("si1", _softIron[1]);
  prefs.putFloat("si2", _softIron[2]);
  prefs.end();
}

// Boat mount (issue #66): device flat against starboard side of centreboard
// case, screen facing starboard, top of screen up, right edge toward the bow.
//   device_X (right edge, fwd)     -> +X (fwd)
//   device_Y (top edge, up)        -> +Z (up)
//   device_Z (out of screen, stbd) -> -Y (port)
void IMU::remapToBoat(float dx, float dy, float dz, float& bx, float& by, float& bz) {
  bx = dx;
  by = -dz;
  bz = dy;
}

void IMU::tick() {
  M5.Imu.update();
  auto d = M5.Imu.getImuData();

  float mx_cal = (d.mag.x - _hardIron[0]) * _softIron[0];
  float my_cal = (d.mag.y - _hardIron[1]) * _softIron[1];
  float mz_cal = (d.mag.z - _hardIron[2]) * _softIron[2];

  float ax, ay, az, gx, gy, gz, mx, my, mz;
  remapToBoat(d.accel.x, d.accel.y, d.accel.z, ax, ay, az);
  remapToBoat(d.gyro.x, d.gyro.y, d.gyro.z, gx, gy, gz);
  remapToBoat(mx_cal, my_cal, mz_cal, mx, my, mz);

  _filter.update(gx, gy, gz, ax, ay, az, mx, my, mz);
}

void IMU::seedInitialAlignment() {
  // Adafruit_Mahony has no public setQuaternion(); the documented workaround
  // is to run a few hundred updates with very high Kp so the filter snaps to
  // the gravity+mag solution in <50 ms instead of the ~5 s a cold start at
  // Kp=5 would take. Drop Kp back afterwards.
  _filter.setKp(50.0f);
  for (int i = 0; i < 500; ++i) {
    tick();
  }
  _filter.setKp(MAHONY_KP);
}

void IMU::fusionTask(void* pvParameters) {
  static_cast<App*>(pvParameters)->getIMU().runFusionLoop();
}

void IMU::runFusionLoop() {
  LOG_INFO(LOG_TAG, "IMU fusion task started — seeding alignment");
  seedInitialAlignment();
  _initialised.store(true, std::memory_order_release);
  LOG_INFO(LOG_TAG, "IMU fusion task running at %.0f Hz", SAMPLE_RATE_HZ);

  const TickType_t period = pdMS_TO_TICKS((uint32_t)(1000.0f / SAMPLE_RATE_HZ));
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&lastWake, period);

    IMUMode mode = _mode.load(std::memory_order_acquire);
    if (mode == IMUMode::CALIBRATING) {
      calibrationTick();
      continue;
    }
    if (mode == IMUMode::CAL_RESULT_PENDING) {
      // Fusion frozen; live values stay at their last fused readings until
      // the user applies/discards. Recording is gated IDLE so no CSV
      // consumer is reading stale values anyway.
      continue;
    }

    // QW8: skip the I²C sensor read + fusion while nothing consumes the output
    // (device idle and asleep). The task still wakes each period — that's
    // cheap; the ~100 I²C transactions/s are what we're avoiding. Fusion
    // resumes and Mahony reconverges within ~1 s the moment the gate opens
    // (recording starts or the display wakes).
    if (_fusionGate && !_fusionGate()) {
      continue;
    }

    tick();

    // Sign convention from issue #66: heel = +roll (+ to starboard);
    // trim = -pitch (+ bow up, nautical/aircraft convention); heading = +yaw.
    float heel = _filter.getRoll();
    float trim = -_filter.getPitch();
    float heading = _filter.getYaw();
    if (heading < 0.0f) heading += 360.0f;

    float yawRad = heading * (float)M_PI / 180.0f;
    float yawX = cosf(yawRad);
    float yawY = sinf(yawRad);
    unsigned long now = millis();

    portENTER_CRITICAL(&_mux);
    _heel = heel;
    _trim = trim;
    _heading = heading;
    if (_accum.n == 0) _accum.startMs = now;
    _accum.heel += heel;
    _accum.trim += trim;
    _accum.headingX += yawX;
    _accum.headingY += yawY;
    _accum.n++;
    portEXIT_CRITICAL(&_mux);
  }
}

void IMU::calibrationTick() {
  auto mask = M5.Imu.update();
  if (mask & m5::IMU_Class::sensor_mask_mag) {
    auto d = M5.Imu.getImuData();
    if (_calSampleCount < MAX_CAL_SAMPLES) {
      _calSamples[_calSampleCount] = {d.mag.x, d.mag.y, d.mag.z};
      _calSampleCount++;
    }
    portENTER_CRITICAL(&_mux);
    if (d.mag.x < _calXMin) _calXMin = d.mag.x;
    if (d.mag.x > _calXMax) _calXMax = d.mag.x;
    if (d.mag.y < _calYMin) _calYMin = d.mag.y;
    if (d.mag.y > _calYMax) _calYMax = d.mag.y;
    if (d.mag.z < _calZMin) _calZMin = d.mag.z;
    if (d.mag.z > _calZMax) _calZMax = d.mag.z;
    portEXIT_CRITICAL(&_mux);
  }

  if (millis() - _calStartMs >= CAL_DURATION_MS) {
    finishCalibration();
  }
}

void IMU::finishCalibration() {
  ImuCal::FitResult fit =
      ImuCal::fit(_calSamples.data(), _calSampleCount, MIN_CAL_SAMPLES);

  CalResult r{};
  r.sampleCount = _calSampleCount;
  r.tooFewSamples = (_calSampleCount < MIN_CAL_SAMPLES);
  if (fit.valid) {
    for (int i = 0; i < 3; ++i) {
      r.hardIron[i] = fit.hardIron[i];
      r.softIron[i] = fit.softIron[i];
    }
    r.residualStd = fit.residualStd;
    r.valid = true;
  }

  portENTER_CRITICAL(&_mux);
  _pendingResult = r;
  portEXIT_CRITICAL(&_mux);

  _mode.store(IMUMode::CAL_RESULT_PENDING, std::memory_order_release);
  LOG_INFO(LOG_TAG, "Cal done: n=%lu valid=%d residual=%.4f",
           (unsigned long)r.sampleCount, (int)r.valid, r.residualStd);
}

bool IMU::startCalibration() {
  IMUMode expected = IMUMode::FUSING;
  if (!_mode.compare_exchange_strong(expected, IMUMode::CALIBRATING,
                                     std::memory_order_acq_rel)) {
    return false;
  }
  portENTER_CRITICAL(&_mux);
  _calSampleCount = 0;
  _calStartMs = millis();
  _calXMin = _calYMin = _calZMin = 1e9f;
  _calXMax = _calYMax = _calZMax = -1e9f;
  _pendingResult = CalResult{};
  portEXIT_CRITICAL(&_mux);
  LOG_INFO(LOG_TAG, "Calibration started — 90 s window");
  return true;
}

void IMU::cancelCalibration() {
  IMUMode mode = _mode.load(std::memory_order_acquire);
  if (mode != IMUMode::CALIBRATING && mode != IMUMode::CAL_RESULT_PENDING) return;
  portENTER_CRITICAL(&_mux);
  _pendingResult = CalResult{};
  portEXIT_CRITICAL(&_mux);
  _mode.store(IMUMode::FUSING, std::memory_order_release);
  LOG_INFO(LOG_TAG, "Calibration cancelled — offsets unchanged");
}

bool IMU::applyPendingCalibration() {
  if (_mode.load(std::memory_order_acquire) != IMUMode::CAL_RESULT_PENDING) return false;

  CalResult r;
  portENTER_CRITICAL(&_mux);
  r = _pendingResult;
  portEXIT_CRITICAL(&_mux);
  if (!r.valid) return false;

  for (int i = 0; i < 3; ++i) {
    _hardIron[i] = r.hardIron[i];
    _softIron[i] = r.softIron[i];
  }
  saveMagCal();

  // New mag offsets invalidate the filter's accumulated state. Reseed.
  _filter.begin(SAMPLE_RATE_HZ);
  _filter.setKi(MAHONY_KI);
  seedInitialAlignment();

  portENTER_CRITICAL(&_mux);
  _pendingResult = CalResult{};
  portEXIT_CRITICAL(&_mux);
  _mode.store(IMUMode::FUSING, std::memory_order_release);
  LOG_INFO(LOG_TAG, "Calibration applied and persisted to NVS");
  return true;
}

void IMU::discardPendingCalibration() {
  if (_mode.load(std::memory_order_acquire) != IMUMode::CAL_RESULT_PENDING) return;
  portENTER_CRITICAL(&_mux);
  _pendingResult = CalResult{};
  portEXIT_CRITICAL(&_mux);
  _mode.store(IMUMode::FUSING, std::memory_order_release);
  LOG_INFO(LOG_TAG, "Pending calibration discarded — offsets unchanged");
}

CalProgress IMU::getCalProgress() const {
  CalProgress p{};
  p.mode = _mode.load(std::memory_order_acquire);
  portENTER_CRITICAL(&_mux);
  p.samples = _calSampleCount;
  p.xMin = _calXMin;
  p.xMax = _calXMax;
  p.yMin = _calYMin;
  p.yMax = _calYMax;
  p.zMin = _calZMin;
  p.zMax = _calZMax;
  unsigned long startMs = _calStartMs;
  portEXIT_CRITICAL(&_mux);
  unsigned long now = millis();
  p.elapsedMs = (now >= startMs) ? (uint32_t)(now - startMs) : 0;
  return p;
}

CalResult IMU::getPendingResult() const {
  CalResult r;
  portENTER_CRITICAL(&_mux);
  r = _pendingResult;
  portEXIT_CRITICAL(&_mux);
  return r;
}

IMUSnapshot IMU::getLatest() const {
  IMUSnapshot s{};
  s.valid = _initialised.load(std::memory_order_acquire);
  if (!s.valid) return s;

  portENTER_CRITICAL(&_mux);
  s.heel = _heel;
  s.trim = _trim;
  s.heading = _heading;
  portEXIT_CRITICAL(&_mux);
  return s;
}

IMUSnapshot IMU::drainAccumulator() {
  IMUSnapshot s{};
  Accum local;
  unsigned long now = millis();

  portENTER_CRITICAL(&_mux);
  local = _accum;
  _accum = Accum{};
  _accum.startMs = now;
  float liveHeel = _heel;
  float liveTrim = _trim;
  float liveHeading = _heading;
  portEXIT_CRITICAL(&_mux);

  unsigned long age = now - local.startMs;
  if (local.n == 0 || age > MAX_WINDOW_MS) {
    s.heel = liveHeel;
    s.trim = liveTrim;
    s.heading = liveHeading;
    s.valid = _initialised.load(std::memory_order_acquire);
  } else {
    s.heel = (float)(local.heel / local.n);
    s.trim = (float)(local.trim / local.n);
    float meanHeading = atan2f((float)local.headingY, (float)local.headingX) * 180.0f / (float)M_PI;
    if (meanHeading < 0.0f) meanHeading += 360.0f;
    s.heading = meanHeading;
    s.valid = true;
  }
  return s;
}
