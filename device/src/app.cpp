#include "app.h"

#include <M5Unified.h>
#include "ui/text_style.h"
#include "utils/boot_recovery.h"
#include "utils/logger.h"
#include "utils/utils.h"

static const char* LOG_TAG = "App";

App::App()
    : calibrationScreen(imu),
      display(displayHardware, touchInput, scheduler) {
  eventQueue = xQueueCreate(50, sizeof(AppEvent));
}

void App::init() {
  M5.begin();

  // Init serial
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_WARN);
  M5.Log.setEnableColor(m5::log_target_serial, false);
  Serial.begin(115200);

  // Enable verbose logging when a serial monitor is connected at boot.
  // USB-CDC enumerates 1-2 s after reset, so give the host a bounded window to
  // open the port — otherwise `if (Serial)` races enumeration and loses on
  // most cold boots. Costs at most 2 s on a battery boot with no host.
  uint32_t serialWait = millis();
  while (!Serial && millis() - serialWait < 2000) {
    delay(10);
  }
  if (Serial) {
    M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_DEBUG);
    Logger::getInstance().setSerialLogging(true);
    Logger::getInstance().setLogLevel(LogLevel::DEBUG);
  }

  // Set the display timezone once, before the logger and any getLocalTime()
  // consumer — the old per-refresh TZ toggle raced localtime_r on other tasks
  // and produced timestamps 12 h off (R10).
  Utils::initTimezone();

  LOG_INFO(LOG_TAG, "Initialising application...");

  // Initialize LittleFS ONCE before logger and recorder
  LOG_INFO(LOG_TAG, "Mounting LittleFS...");
  if (!LittleFS.begin(false)) {  // format_on_fail = false for safety
    LOG_ERROR(LOG_TAG, "FATAL: Failed to mount LittleFS");
    LOG_ERROR(LOG_TAG, "Device requires flash clear or filesystem recovery");
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextColor(TFT_WHITE);
    ui::applyTextStyle(ui::TextStyle::Body);
    M5.Display.setCursor(10, 100);
    M5.Display.println("FILESYSTEM ERROR");
    M5.Display.println("Requires flash clear");
    while (true) {
      delay(1000);
    }
  }
  LOG_INFO(LOG_TAG, "LittleFS mounted successfully");

  bool recoveryNeeded = BootRecovery::checkAndMarkPessimistic();

  if (recoveryNeeded) {
    M5.Log(ESP_LOG_WARN, "[App] Previous boot crashed during LFS write — entering recovery");
  } else {
    if (!Logger::getInstance().init()) {
      LOG_ERROR(LOG_TAG, "Failed to initialize logger");
    }
    BootRecovery::markClean();
  }

  // Register screens with Display
  display.registerScreen(ScreenState::MAIN_MENU, &mainMenuScreen);
  display.registerScreen(ScreenState::SPEED_SCREEN, &speedScreen);
  display.registerScreen(ScreenState::DETAIL_SCREEN, &detailScreen);
  display.registerScreen(ScreenState::WIFI_SCREEN, &wifiScreen);
  display.registerScreen(ScreenState::SYNC_SCREEN, &syncScreen);
  display.registerScreen(ScreenState::CALIBRATION_SCREEN, &calibrationScreen);

  // Wire ScreenContext (the only collaborator screens see) and pass it to Display.
  screenContext.init(
      &display,
      [this](ButtonId id) { postEvent(EventType::BUTTON_PRESS, id); },
      [this]() { return static_cast<int>(storage.localTracks().size()); });
  display.init(&screenContext, [this]() { return appController.getGPSState(); });

  // Serialise SD (SPI-bus) access against display drawing on the shared FSPI
  // bus (B4). Storage holds this per SD transaction.
  storage.setSpiLock([this]() { display.lockSPI(); }, [this]() { display.unlockSPI(); });

  recording.init(
      [this]() { return appController.getGPSState() == GPSState::LOCKED; },
      [this](RecordingState oldState, RecordingState newState) {
        display.updateStatusBar(appController.getGPSState(), newState);
      },
      [this]() { return imu.drainAccumulator(); });

  connectivity.init(
      [this](WifiState oldState, WifiState newState) { display.updateWifi(newState); },
      []() { Utils::syncTimeWithNTP(); },
      &storage,
      [this]() { espnowTelemetry.restart(); });

  espnowTelemetry.begin(
      [this]() { return composeTelemetry(); },
      [this](bool desiredOn) {
        // Runs on the WiFi task — serialise through the event queue so
        // AppController owns the state machine, exactly like BUTTON_PRESS.
        AppEvent event;
        event.type = EventType::SET_RECORDING;
        event.data.recordingDesired = desiredOn;
        postEvent(event);
      },
      [this](const moana::Event& wire) {
        // Watch event (issue #139) — same serialisation: AppController owns
        // the dedupe/ack-frontier state, so it must see arrivals in order.
        AppEvent event;
        event.type = EventType::MODE_EVENT;
        event.data.modeEvent = wire;
        postEvent(event);
      });

  appController.init({
      .display = buildDisplayCallbacks(),
      .recording = buildRecordingCallbacks(),
      .connectivity = buildConnectivityCallbacks(),
      .storage = buildStorageCallbacks(),
      .logger = {.initLogger = []() { return Logger::getInstance().init(); }},
      .imu = buildIMUCallbacks(),
      // Session distance integrates with TinyGPSPlus' great-circle distance
      // (issue #142); injected so AppController stays natively testable.
      .distanceBetween =
          [](double lat1, double lng1, double lat2, double lng2) {
            return TinyGPSPlus::distanceBetween(lat1, lng1, lat2, lng2);
          },
      .postEvent = [this](const AppEvent& e) { postEvent(e); },
      .postEventCritical = [this](const AppEvent& e) { postEvent(e, true); },
      .markBootClean = []() { BootRecovery::markClean(); },
  });

  xTaskCreate(appControllerTask, "AppController", 8192, this, 3, nullptr);

  gps.init(this);
  imu.init();
  // QW8: run the 100 Hz fusion loop only when something consumes its output —
  // recording is active (CSV rows) or the display is awake (potential live
  // read). Idle + asleep, the sensor isn't polled, saving ~100 I²C txns/s.
  imu.setFusionGate([this]() {
    return recording.getState() == RecordingState::ACTIVE ||
           display.getDisplaySleepState() == DisplaySleepState::AWAKE;
  });

  xTaskCreate(GPS::dataTask, "GPS Update", 4096, this, 2, nullptr);
  xTaskCreate(GPS::healthTask, "GPS Health", 4096, this, 2, nullptr);
  // Pin IMU fusion to core 1 with GPS so they share the I²C bus without
  // contention from networking on core 0. Skip entirely when no IMU was
  // detected — otherwise the task fuses garbage and marks CSV rows valid (B9).
  if (imu.isPresent()) {
    xTaskCreatePinnedToCore(IMU::fusionTask, "IMU Fusion", 6144, this, 2, nullptr, 1);
  } else {
    LOG_WARN(LOG_TAG, "IMU not present — fusion task not spawned; IMU CSV columns stay empty");
  }
  xTaskCreate(Utils::monitoringTask, "Monitoring", 4096, this, 2, nullptr);
  xTaskCreate(Display::touchTask, "TouchMonitor", 4096, &display, 2, nullptr);
  xTaskCreate(Display::gpsDataTask, "GPSScreenUpdates", 4096, &display, 2, nullptr);

  if (recoveryNeeded) {
    appController.setRecoveryState(RecoveryState::NEEDED);
    display.showScreen(ScreenState::SYNC_SCREEN);
    display.showRecoveryMessage();
    appController.startSync();
  }
}

GPS& App::getGPS() {
  return gps;
}

// ============================================
// Event Posting
// ============================================

void App::postEvent(const AppEvent& event, bool critical) {
  // Critical events must never be dropped: losing a SYNC_COMPLETE leaves
  // AppController stuck in SYNCING forever, refusing every future sync until
  // reboot (R3). Block until a slot frees instead. Everything else (GPS
  // data/state, button presses) is safe to drop under backpressure — there is
  // always another sample or tap right behind it.
  TickType_t timeout = critical ? portMAX_DELAY : pdMS_TO_TICKS(100);
  if (xQueueSend(eventQueue, &event, timeout) != pdTRUE) {
    LOG_WARN(LOG_TAG, "Event queue full, dropped event type: %s",
             App::eventTypeToString(event.type));
  }
}

// ============================================
// App Controller Task
// ============================================

void App::appControllerTask(void* pvParameters) {
  LOG_INFO(LOG_TAG, "App Controller task started");
  App* app = static_cast<App*>(pvParameters);
  AppEvent event;

  while (true) {
    if (xQueueReceive(app->eventQueue, &event, portMAX_DELAY) == pdTRUE) {
      app->appController.process(event);
    }
  }
}

// ============================================
// Callback Builders
// ============================================

DisplayCallbacks App::buildDisplayCallbacks() {
  return {
      .apply = [this](const DisplayCmd& cmd) { display.apply(cmd); },
      .getScreenState = [this]() { return display.getScreenState(); },
  };
}

RecordingCallbacks App::buildRecordingCallbacks() {
  return {
      .toggle = [this]() { recording.toggle(); },
      .setRecording = [this](bool on) { recording.setRecording(on); },
      .feedGPSData = [this](const GPSData& d) { recording.feedGPSData(d); },
      .getState = [this]() { return recording.getState(); },
      .appendEvent =
          [this](uint8_t kind, uint8_t mode, uint32_t utc) {
            return recording.appendEvent(kind, mode, utc);
          },
  };
}

ConnectivityCallbacks App::buildConnectivityCallbacks() {
  return {
      .toggle = [this]() { connectivity.toggle(); },
      .receiveGPSData =
          [this](const GPSData& d) {
            connectivity.receiveGPSData(d);
            feedTelemetryGPS(d);
          },
  };
}

// ============================================
// ESP-NOW Telemetry Payload
// ============================================

void App::feedTelemetryGPS(const GPSData& data) {
  portENTER_CRITICAL(&espnowGpsMux);
  espnowGps = data;
  espnowGpsMs = millis();
  portEXIT_CRITICAL(&espnowGpsMux);
}

static uint8_t toWireRecordingState(RecordingState s) {
  switch (s) {
    case RecordingState::IDLE:
      return moana::RECORDING_IDLE;
    case RecordingState::STARTING:
      return moana::RECORDING_STARTING;
    case RecordingState::ACTIVE:
      return moana::RECORDING_ACTIVE;
    case RecordingState::STOPPING:
      return moana::RECORDING_STOPPING;
    case RecordingState::FAILED:
      return moana::RECORDING_FAILED;
  }
  return moana::RECORDING_IDLE;
}

// Called from the ESP-NOW send task at 1Hz. Everything it reads is either
// spinlock-guarded (GPS latch) or atomic (recording state, IMU snapshot).
moana::Telemetry App::composeTelemetry() {
  GPSData gps;
  unsigned long feedMs;
  portENTER_CRITICAL(&espnowGpsMux);
  gps = espnowGps;
  feedMs = espnowGpsMs;
  portEXIT_CRITICAL(&espnowGpsMux);

  // GPS_DATA events only fire on valid fixes, so freshness implies a fix.
  bool fresh = feedMs != 0 && millis() - feedMs < ESPNOW_FIX_FRESH_MS;

  moana::Telemetry t = {};
  t.fix_valid = fresh ? 1 : 0;
  if (fresh) {
    t.lat = gps.lat;
    t.lng = gps.lng;
    t.sog_knots = (float)gps.speed;
    t.cog_deg = (float)gps.course;
    t.altitude_m = (float)gps.altitude;
    t.hdop = (float)gps.hdop;
    t.satellites = (uint8_t)gps.satellites;
    t.utc_seconds = moana::utcEpochFromGps(gps.gps_date, gps.gps_time);
  }

  IMUSnapshot imuSnap = imu.getLatest();
  if (imuSnap.valid) {
    t.heel_deg = imuSnap.heel;
    t.trim_deg = imuSnap.trim;
    t.heading_deg = imuSnap.heading;
  }

  t.recording_state = toWireRecordingState(recording.getState());
  // Watch-event echo (issue #139): the mode the boat last applied and the
  // contiguous ack frontier the watch trims its resend queue against. Both
  // atomics owned by AppController.
  t.current_mode = appController.getCurrentMode();
  t.last_event_seq_acked = appController.getEventAckFrontier();
  // Session distance echo (issue #142): metres integrated on the boat while
  // recording is ACTIVE — 0 at start, held after stop. The watch derives leg
  // and session distances purely by diffing this value.
  t.cumulative_distance_m = appController.getCumulativeDistanceM();
  return t;
}

IMUCallbacks App::buildIMUCallbacks() {
  return {
      .startCalibration = [this]() { return imu.startCalibration(); },
      .cancelCalibration = [this]() { imu.cancelCalibration(); },
      .applyPendingCalibration = [this]() { return imu.applyPendingCalibration(); },
      .discardPendingCalibration = [this]() { imu.discardPendingCalibration(); },
  };
}

StorageCallbacks App::buildStorageCallbacks() {
  return {
      .localTracks = [this]() { return storage.localTracks(); },
      .startSync =
          [this](bool del, std::function<void(const SyncResult&)> cb) {
            return storage.startSync(del, cb);
          },
      .formatLocal = [this]() { return storage.formatLocal(); },
  };
}

const char* App::eventTypeToString(EventType type) {
  switch (type) {
    case EventType::GPS_STATE:
      return "GPS_STATE";
    case EventType::GPS_DATA:
      return "GPS_DATA";
    case EventType::BUTTON_PRESS:
      return "BUTTON_PRESS";
    case EventType::SYNC_COMPLETE:
      return "SYNC_COMPLETE";
    case EventType::SET_RECORDING:
      return "SET_RECORDING";
    case EventType::MODE_EVENT:
      return "MODE_EVENT";
    default:
      return "UNKNOWN_EVENT";
  }
}
