#include <Arduino.h>
#include <M5Unified.h>
#include <Adafruit_AHRS.h>
#include <Preferences.h>

// IMU bring-up sketch for issue #66.
//
// Modes:
//   FUSED   - heel/trim/heading on LCD; touch-and-hold 2 s triggers CAL_RUN
//   RAW     - stream MotionCal-format Raw: lines at 100 Hz over serial
//   RATE    - measure actual M5.Imu.update() loop period for 5 s
//   CAL_RUN - 90 s on-device mag calibration; saves to NVS, returns to FUSED
//   CAL     - print currently-loaded mag calibration
//   STOP    - return to idle (menu reachable via STOP)
//
// Filter: Mahony (auto gyro-bias correction).

enum class Mode { IDLE, RAW, FUSED, RATE, CAL_RUN };
static Mode mode = Mode::FUSED;  // boot directly into FUSED so device works standalone

Adafruit_Mahony filter;
constexpr float SAMPLE_RATE_HZ = 100.0f;

// Magnetometer calibration. Loaded from NVS at boot; defaults below are used
// only on a fresh device with no stored calibration. Run CAL_RUN to overwrite.
// Captured in a clean magnetic environment (open park) — recalibrate in situ
// once installed on the boat.
static float MAG_HARD_IRON[3] = { -387.072f, -108.231f, -198.554f };
static float MAG_SOFT_IRON[3] = {    0.9830f,    0.9989f,    1.0188f };

static constexpr const char* PREF_NAMESPACE = "magcal";

static void loadMagCalFromNVS() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE, true);
  if (prefs.isKey("hi0")) {
    MAG_HARD_IRON[0] = prefs.getFloat("hi0");
    MAG_HARD_IRON[1] = prefs.getFloat("hi1");
    MAG_HARD_IRON[2] = prefs.getFloat("hi2");
    MAG_SOFT_IRON[0] = prefs.getFloat("si0");
    MAG_SOFT_IRON[1] = prefs.getFloat("si1");
    MAG_SOFT_IRON[2] = prefs.getFloat("si2");
    Serial.println("Loaded mag calibration from NVS");
  } else {
    Serial.println("No mag calibration in NVS — using compiled defaults");
  }
  prefs.end();
}

static void saveMagCalToNVS() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE, false);
  prefs.putFloat("hi0", MAG_HARD_IRON[0]);
  prefs.putFloat("hi1", MAG_HARD_IRON[1]);
  prefs.putFloat("hi2", MAG_HARD_IRON[2]);
  prefs.putFloat("si0", MAG_SOFT_IRON[0]);
  prefs.putFloat("si1", MAG_SOFT_IRON[1]);
  prefs.putFloat("si2", MAG_SOFT_IRON[2]);
  prefs.end();
}

// Boat mount: device flat against starboard side of the centreboard case,
// screen facing starboard, top of screen up, right edge of screen forward.
// Remap device frame -> canonical AHRS frame (X forward, Y port, Z up) so
// filter.getRoll/Pitch/Yaw map directly to heel/trim/heading.
//   device_X (right edge, fwd)  -> +X (fwd)
//   device_Y (top edge, up)     -> +Z (up)
//   device_Z (out of screen, stbd) -> -Y (port)
//
// Set to 0 when testing held flat (screen up) — the remap assumes the boat
// mounting orientation, so flat-held data must be passed through directly.
#define USE_BOAT_REMAP 1

static inline void remapToBoat(float dx, float dy, float dz,
                               float& bx, float& by, float& bz) {
#if USE_BOAT_REMAP
  bx =  dx;
  by = -dz;
  bz =  dy;
#else
  bx = dx;
  by = dy;
  bz = dz;
#endif
}

static void printMenu() {
  Serial.println();
  Serial.println("=== IMU Sketch ===");
  Serial.printf("IMU type: %d (1=sh200q 2=mpu6050 3=mpu6886 4=mpu9250 5=bmi270)\n",
                (int)M5.Imu.getType());
  Serial.println("Commands:");
  Serial.println("  RAW      - MotionCal raw stream @100 Hz");
  Serial.println("  FUSED    - fused heel/trim/heading on LCD");
  Serial.println("  RATE     - measure update rate for 5 s");
  Serial.println("  CAL_RUN  - 90 s mag calibration, save to NVS");
  Serial.println("  CAL      - print loaded mag calibration");
  Serial.println("  STOP     - return to idle");
  Serial.println("Ready...");
}

static void printMagCal() {
  Serial.println("--- Mag calibration (currently loaded) ---");
  Serial.printf("  hard_iron = { %+.3f, %+.3f, %+.3f }\n",
                MAG_HARD_IRON[0], MAG_HARD_IRON[1], MAG_HARD_IRON[2]);
  Serial.printf("  soft_iron = { %.4f, %.4f, %.4f }\n",
                MAG_SOFT_IRON[0], MAG_SOFT_IRON[1], MAG_SOFT_IRON[2]);
}

static void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if      (cmd == "RAW")     mode = Mode::RAW;
  else if (cmd == "FUSED")   mode = Mode::FUSED;
  else if (cmd == "RATE")    mode = Mode::RATE;
  else if (cmd == "CAL_RUN") mode = Mode::CAL_RUN;
  else if (cmd == "STOP")    { mode = Mode::IDLE; printMenu(); }
  else if (cmd == "CAL")     printMagCal();
  else                       { Serial.println("Unknown: " + cmd); printMenu(); }
}

// MotionCal expects:
//   accel: int16, 8192 LSB/g
//   gyro:  int16, 16 LSB per deg/s
//   mag:   int16, 10 LSB per uT
// M5Unified returns accel in g, gyro in deg/s, mag in uT.
static void loopRaw() {
  static uint32_t lastUs = 0;
  const uint32_t periodUs = 1'000'000UL / 100;
  uint32_t now = micros();
  if (now - lastUs < periodUs) return;
  lastUs = now;

  M5.Imu.update();
  auto d = M5.Imu.getImuData();

  int16_t ax = (int16_t)lroundf(d.accel.x * 8192.0f);
  int16_t ay = (int16_t)lroundf(d.accel.y * 8192.0f);
  int16_t az = (int16_t)lroundf(d.accel.z * 8192.0f);
  int16_t gx = (int16_t)lroundf(d.gyro.x * 16.0f);
  int16_t gy = (int16_t)lroundf(d.gyro.y * 16.0f);
  int16_t gz = (int16_t)lroundf(d.gyro.z * 16.0f);
  int16_t mx = (int16_t)lroundf(d.mag.x * 10.0f);
  int16_t my = (int16_t)lroundf(d.mag.y * 10.0f);
  int16_t mz = (int16_t)lroundf(d.mag.z * 10.0f);

  Serial.printf("Raw:%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                ax, ay, az, gx, gy, gz, mx, my, mz);
}

static void drawFusedScreen(float heel, float trim, float heading) {
  // Use a sprite to avoid flicker from clearing+drawing on the live LCD.
  static M5Canvas canvas(&M5.Display);
  static bool inited = false;
  if (!inited) {
    canvas.createSprite(M5.Display.width(), M5.Display.height());
    inited = true;
  }

  canvas.fillSprite(TFT_BLACK);
  canvas.setTextDatum(top_left);

  // Heading: large central readout, primary value for the park test.
  canvas.setTextColor(TFT_GREEN);
  canvas.setTextSize(8);
  canvas.setCursor(20, 60);
  canvas.printf("%5.1f", heading);

  canvas.setTextSize(2);
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(20, 20);
  canvas.print("HEADING");

  // Heel + trim, smaller, beneath.
  canvas.setTextSize(2);
  canvas.setTextColor(TFT_LIGHTGREY);
  canvas.setCursor(20, 170);
  canvas.printf("heel %+6.1f", heel);
  canvas.setCursor(20, 200);
  canvas.printf("trim %+6.1f", trim);

  // Hint for the user.
  canvas.setTextColor(TFT_DARKGREY);
  canvas.setCursor(M5.Display.width() - 130, M5.Display.height() - 20);
  canvas.print("hold 2s = cal");

  canvas.pushSprite(0, 0);
}

static void drawCalScreen(uint32_t elapsedMs, uint32_t totalMs, uint32_t samples,
                          float xMin, float xMax, float yMin, float yMax,
                          float zMin, float zMax) {
  static M5Canvas canvas(&M5.Display);
  static bool inited = false;
  if (!inited) {
    canvas.createSprite(M5.Display.width(), M5.Display.height());
    inited = true;
  }

  canvas.fillSprite(TFT_BLACK);
  canvas.setTextDatum(top_left);

  canvas.setTextColor(TFT_YELLOW);
  canvas.setTextSize(3);
  canvas.setCursor(20, 15);
  canvas.print("CALIBRATING");

  canvas.setTextSize(2);
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(20, 55);
  canvas.print("Wave through ALL");
  canvas.setCursor(20, 75);
  canvas.print("orientations");

  // Big remaining-time readout.
  uint32_t remainingS = (totalMs > elapsedMs) ? (totalMs - elapsedMs + 999) / 1000 : 0;
  canvas.setTextColor(TFT_GREEN);
  canvas.setTextSize(6);
  canvas.setCursor(140, 105);
  canvas.printf("%2lus", (unsigned long)remainingS);

  // Per-axis ranges so the user can see which axis still needs sweep.
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_LIGHTGREY);
  canvas.setCursor(20, 175);
  canvas.printf("x [%+5.0f .. %+5.0f]", xMin, xMax);
  canvas.setCursor(20, 190);
  canvas.printf("y [%+5.0f .. %+5.0f]", yMin, yMax);
  canvas.setCursor(20, 205);
  canvas.printf("z [%+5.0f .. %+5.0f]", zMin, zMax);
  canvas.setCursor(20, 220);
  canvas.printf("samples = %lu", (unsigned long)samples);

  canvas.pushSprite(0, 0);
}

static bool checkLongTouchToCalibrate() {
  static uint32_t pressStartMs = 0;
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.isPressed()) {
    if (pressStartMs == 0) pressStartMs = millis();
    if (millis() - pressStartMs >= 2000) {
      pressStartMs = 0;
      return true;
    }
  } else {
    pressStartMs = 0;
  }
  return false;
}

static void loopFused() {
  static uint32_t lastFilterUs = 0;
  static uint32_t lastPrintMs = 0;
  static double sumHeel = 0, sumTrim = 0;
  static double sumHeadingX = 0, sumHeadingY = 0;  // average via unit vector to handle 359°->0° wrap
  static uint32_t sampleCount = 0;
  const uint32_t filterPeriodUs = 1'000'000UL / (uint32_t)SAMPLE_RATE_HZ;
  const uint32_t printPeriodMs = 500;  // 2 Hz, averaged

  if (checkLongTouchToCalibrate()) {
    Serial.println("Long touch detected — entering CAL_RUN");
    mode = Mode::CAL_RUN;
    return;
  }

  uint32_t now = micros();
  if (now - lastFilterUs >= filterPeriodUs) {
    lastFilterUs = now;
    M5.Imu.update();
    auto d = M5.Imu.getImuData();

    float mx_cal = (d.mag.x - MAG_HARD_IRON[0]) * MAG_SOFT_IRON[0];
    float my_cal = (d.mag.y - MAG_HARD_IRON[1]) * MAG_SOFT_IRON[1];
    float mz_cal = (d.mag.z - MAG_HARD_IRON[2]) * MAG_SOFT_IRON[2];

    float ax, ay, az, gx, gy, gz, mx, my, mz;
    remapToBoat(d.accel.x, d.accel.y, d.accel.z, ax, ay, az);
    remapToBoat(d.gyro.x,  d.gyro.y,  d.gyro.z,  gx, gy, gz);
    remapToBoat(mx_cal,    my_cal,    mz_cal,    mx, my, mz);

    filter.update(gx, gy, gz, ax, ay, az, mx, my, mz);

    sumHeel += filter.getRoll();
    sumTrim += -filter.getPitch();   // flip so bow-up = positive (nautical/aircraft convention)
    float yawRad = filter.getYaw() * (float)M_PI / 180.0f;
    sumHeadingX += cosf(yawRad);
    sumHeadingY += sinf(yawRad);
    sampleCount++;
  }

  uint32_t nowMs = millis();
  if (nowMs - lastPrintMs >= printPeriodMs && sampleCount > 0) {
    lastPrintMs = nowMs;
    float heel = (float)(sumHeel / sampleCount);
    float trim = (float)(sumTrim / sampleCount);
    float heading = atan2f((float)sumHeadingY, (float)sumHeadingX) * 180.0f / (float)M_PI;
    if (heading < 0) heading += 360.0f;
    Serial.printf("heel=%7.2f  trim=%7.2f  heading=%7.2f  (n=%lu)\n",
                  heel, trim, heading, (unsigned long)sampleCount);
    drawFusedScreen(heel, trim, heading);
    sumHeel = sumTrim = sumHeadingX = sumHeadingY = 0;
    sampleCount = 0;
  }
}

static void loopRate() {
  static uint32_t startMs = 0;
  static uint32_t pollCount = 0;
  static uint32_t accelCount = 0;
  static uint32_t gyroCount = 0;
  static uint32_t magCount = 0;

  if (startMs == 0) {
    startMs = millis();
    pollCount = accelCount = gyroCount = magCount = 0;
    Serial.println("Measuring update rate for 5 s...");
  }

  auto mask = M5.Imu.update();
  pollCount++;
  if (mask & m5::IMU_Class::sensor_mask_accel) accelCount++;
  if (mask & m5::IMU_Class::sensor_mask_gyro)  gyroCount++;
  if (mask & m5::IMU_Class::sensor_mask_mag)   magCount++;

  if (millis() - startMs >= 5000) {
    float elapsedS = (millis() - startMs) / 1000.0f;
    Serial.printf("Fresh samples in %.2fs:\n", elapsedS);
    Serial.printf("  accel = %5lu  (%5.1f Hz)\n",
                  (unsigned long)accelCount, accelCount / elapsedS);
    Serial.printf("  gyro  = %5lu  (%5.1f Hz)\n",
                  (unsigned long)gyroCount,  gyroCount  / elapsedS);
    Serial.printf("  mag   = %5lu  (%5.1f Hz)\n",
                  (unsigned long)magCount,   magCount   / elapsedS);
    Serial.printf("Poll loop ran at %.0f Hz (%lu calls)\n",
                  pollCount / elapsedS, (unsigned long)pollCount);
    mode = Mode::IDLE;
    startMs = 0;
    printMenu();
  }
}

static void loopCal() {
  static uint32_t startMs = 0;
  static uint32_t sampleCount = 0;
  static uint32_t lastDrawMs = 0;
  static float xMin, xMax, yMin, yMax, zMin, zMax;
  const uint32_t durationMs = 90000;

  if (startMs == 0) {
    startMs = millis();
    lastDrawMs = 0;
    sampleCount = 0;
    xMin = yMin = zMin =  1e9f;
    xMax = yMax = zMax = -1e9f;
    Serial.println("CAL_RUN started — wave device through all orientations for 90 s");
  }

  auto mask = M5.Imu.update();
  if (mask & m5::IMU_Class::sensor_mask_mag) {
    auto d = M5.Imu.getImuData();
    if (d.mag.x < xMin) xMin = d.mag.x;
    if (d.mag.x > xMax) xMax = d.mag.x;
    if (d.mag.y < yMin) yMin = d.mag.y;
    if (d.mag.y > yMax) yMax = d.mag.y;
    if (d.mag.z < zMin) zMin = d.mag.z;
    if (d.mag.z > zMax) zMax = d.mag.z;
    sampleCount++;
  }

  uint32_t elapsed = millis() - startMs;
  if (millis() - lastDrawMs >= 200) {
    lastDrawMs = millis();
    drawCalScreen(elapsed, durationMs, sampleCount,
                  xMin, xMax, yMin, yMax, zMin, zMax);
  }

  if (elapsed >= durationMs) {
    if (sampleCount < 200) {
      Serial.println("CAL_RUN aborted — not enough mag samples (motion too brief)");
      startMs = 0;
      mode = Mode::FUSED;
      return;
    }

    float rX = (xMax - xMin) / 2.0f;
    float rY = (yMax - yMin) / 2.0f;
    float rZ = (zMax - zMin) / 2.0f;
    float avgR = (rX + rY + rZ) / 3.0f;

    MAG_HARD_IRON[0] = (xMax + xMin) / 2.0f;
    MAG_HARD_IRON[1] = (yMax + yMin) / 2.0f;
    MAG_HARD_IRON[2] = (zMax + zMin) / 2.0f;
    MAG_SOFT_IRON[0] = avgR / rX;
    MAG_SOFT_IRON[1] = avgR / rY;
    MAG_SOFT_IRON[2] = avgR / rZ;

    saveMagCalToNVS();
    Serial.printf("CAL_RUN done — %lu samples\n", (unsigned long)sampleCount);
    printMagCal();

    // Reset filter so it starts fresh with the new mag calibration.
    filter.begin(SAMPLE_RATE_HZ);
    filter.setKp(5.0f);
    filter.setKi(0.0f);

    startMs = 0;
    mode = Mode::FUSED;
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(500);

  if (!M5.Imu.isEnabled()) {
    Serial.println("FATAL: IMU not detected");
    while (true) delay(1000);
  }

  // Disable on-the-fly calibration so RAW streams reflect what MotionCal sees.
  M5.Imu.setCalibration(0, 0, 0);
  M5.Imu.loadOffsetFromNVS();

  loadMagCalFromNVS();

  filter.begin(SAMPLE_RATE_HZ);
  // Default Kp=0.5 takes ~60 s to converge from cold start. 5.0 settles in
  // ~5 s with negligible jitter increase for our use case.
  filter.setKp(5.0f);
  filter.setKi(0.0f);

  printMenu();
}

void loop() {
  handleSerial();
  switch (mode) {
    case Mode::RAW:     loopRaw();   break;
    case Mode::FUSED:   loopFused(); break;
    case Mode::RATE:    loopRate();  break;
    case Mode::CAL_RUN: loopCal();   break;
    case Mode::IDLE:    delay(10);   break;
  }
}
