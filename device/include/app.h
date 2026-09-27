#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <type_traits>

#include <LittleFS.h>

#include "connectivity/connectivity.h"
#include "connectivity/espnow_telemetry.h"
#include "espnow_protocol.h"
#include "events/app_controller.h"
#include "events/app_event.h"
#include "hardware/gps.h"
#include "hardware/imu.h"
#include "recording/recorder.h"
#include "storage/storage.h"
#include "ui/display.h"
#include "ui/ports/m5_display_hardware.h"
#include "ui/ports/m5_touch_input.h"
#include "ui/ports/arduino_scheduler.h"
#include "ui/screen_context.h"
#include "ui/screens/calibration_screen.h"
#include "ui/screens/detail_screen.h"
#include "ui/screens/main_menu_screen.h"
#include "ui/screens/speed_screen.h"
#include "ui/screens/sync_screen.h"
#include "ui/screens/wifi_screen.h"

class App {
 public:
  App();
  void init();

  // Event posting (called from tasks). `critical` events block until the queue
  // drains instead of dropping under backpressure — used for SYNC_COMPLETE,
  // whose loss would strand AppController in SYNCING forever (R3).
  void postEvent(const AppEvent& event, bool critical = false);

  template <typename T>
  void postEvent(EventType type, const T& payload) {
    AppEvent event;
    event.type = type;
    if constexpr (std::is_same_v<T, GPSData>)
      event.data.gpsData = payload;
    else if constexpr (std::is_same_v<T, GPSState>)
      event.data.gpsState = payload;
    else if constexpr (std::is_same_v<T, ButtonId>)
      event.data.buttonId = payload;
    else if constexpr (std::is_same_v<T, SyncResult>)
      event.data.syncResult = payload;
    else
      static_assert(!sizeof(T), "Unsupported event payload type");
    postEvent(event);
  }

  // Accessors
  GPS& getGPS();
  IMU& getIMU() { return imu; }
  GPSState getGPSState() const { return appController.getGPSState(); }
  Display& getDisplay() { return display; }

  // Tasks
  static void appControllerTask(void* pvParameters);

 private:
  // Display port implementations (must be declared before display)
  M5DisplayHardware displayHardware;
  M5TouchInput touchInput;
  ArduinoScheduler scheduler;

  // Hardware (declared before screens so CalibrationScreen can bind a
  // const IMU& at construction time).
  GPS gps;
  IMU imu;

  // Screens (must be declared before display registration)
  MainMenuScreen mainMenuScreen;
  SpeedScreen speedScreen;
  DetailScreen detailScreen;
  WifiScreen wifiScreen;
  SyncScreen syncScreen;
  CalibrationScreen calibrationScreen;

  // Subsystems
  Display display;
  ScreenContext screenContext;
  Recorder recording;
  Connectivity connectivity;
  EspnowTelemetry espnowTelemetry;
  Storage storage;

  // ESP-NOW telemetry payload — App owns composition (recording state, GPS,
  // IMU); EspnowTelemetry only stamps seq and transmits (issue #138).
  // Latest valid GPS fix, latched off the GPS_DATA event path and read from
  // the ESP-NOW send task, hence the spinlock.
  GPSData espnowGps{};
  unsigned long espnowGpsMs = 0;  // millis() of last feed, 0 = never
  portMUX_TYPE espnowGpsMux = portMUX_INITIALIZER_UNLOCKED;
  // A GPS_DATA event this recent means the fix is current.
  static constexpr unsigned long ESPNOW_FIX_FRESH_MS = 3000;

  void feedTelemetryGPS(const GPSData& data);
  moana::Telemetry composeTelemetry();

  // Event processing
  AppController appController;

  // FreeRTOS primitives
  QueueHandle_t eventQueue;

  // Callback builders for AppController wiring
  DisplayCallbacks buildDisplayCallbacks();
  RecordingCallbacks buildRecordingCallbacks();
  ConnectivityCallbacks buildConnectivityCallbacks();
  StorageCallbacks buildStorageCallbacks();
  IMUCallbacks buildIMUCallbacks();

  static const char* eventTypeToString(EventType type);
};
