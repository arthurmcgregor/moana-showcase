#pragma once

#include <array>
#include <functional>
#include <mutex>

#include "hardware/gps_types.h"
#include "storage/sync_types.h"
#include "ui/display_cmd.h"
#include "ui/ports/display_hardware.h"
#include "ui/ports/touch_input.h"
#include "ui/ports/scheduler.h"
#include "ui/screens/screen.h"
#include "ui/status_bar.h"
#include "types.h"

class ScreenContext;

class Display {
 public:
  using GetGPSStateFn = std::function<GPSState()>;

  Display(DisplayHardware& hardware, TouchInput& touch, Scheduler& scheduler);

  void init(ScreenContext* context, GetGPSStateFn getGPSState);

  // Screen registration (called during init, before tasks start)
  void registerScreen(ScreenState id, Screen* screen);

  // Single render-side entry point dispatched by AppController.
  // Switches on cmd.kind and forwards to the typed methods below.
  void apply(const DisplayCmd& cmd);

  // Screen management
  void showScreen(ScreenState screen);

  // State updates
  void updateStatusBar(GPSState gpsState, RecordingState recordingState);
  void updateGPSData(const GPSData& data);
  void updateWifi(WifiState state);
  void updateSync(SyncState state, int trackCount);
  void showSyncResult(const SyncResult& result);
  void showSyncDeferred();

  // Status bar updates
  void updateBattery(int level);
  void updateTime(uint8_t hours, uint8_t minutes, uint8_t seconds);

  // Display sleep/wake
  void sleep();
  void wake();

  // Full-screen messages
  void showRecoveryMessage();
  void showFatalError(const char* message);

  // SPI lock for external users (e.g., Storage SD card access).
  // Returns an RAII guard.
  class SPIGuard {
   public:
    explicit SPIGuard(DisplayHardware& hw) : hw_(hw) { hw_.lockSPI(); }
    ~SPIGuard() { if (!released_) hw_.unlockSPI(); }
    SPIGuard(const SPIGuard&) = delete;
    SPIGuard& operator=(const SPIGuard&) = delete;
    SPIGuard(SPIGuard&& other) noexcept : hw_(other.hw_), released_(other.released_) {
      other.released_ = true;
    }
    SPIGuard& operator=(SPIGuard&&) = delete;

   private:
    DisplayHardware& hw_;
    bool released_ = false;
    friend class Display;
  };
  SPIGuard acquireSPI();

  // Raw SPI lock/unlock for external users whose critical section outlives a
  // single scope (e.g. Storage's SD transactions, where the mount is handed to
  // a TrackFile that reads and unmounts later). Prefer acquireSPI() otherwise.
  // The underlying mutex is recursive, so nesting from one task is safe.
  void lockSPI() { hardware_.lockSPI(); }
  void unlockSPI() { hardware_.unlockSPI(); }

  // State getters
  ScreenState getScreenState() const { return screenState_; }
  DisplaySleepState getDisplaySleepState() const { return sleepState_; }
  // Current GPS fix state (owned by AppController; read via the injected
  // functor). Lets a screen reflect fix/staleness at render time, not only on
  // the transition event. NOT_STARTED before init() wires the functor.
  GPSState getGPSState() const {
    return getGPSState_ ? getGPSState_() : GPSState::NOT_STARTED;
  }

  // Single-iteration tick functions — external scheduler owns the loop
  void touchTick();
  void gpsDisplayTick();

  // FreeRTOS task entry points (thin wrappers around tick functions)
  static void touchTask(void* pvParameters);
  static void gpsDataTask(void* pvParameters);

 private:
  Screen* activeScreen() const;

  // Screen transition validation
  bool canTransitionToScreen(ScreenState newState) const;

  // Display helpers
  GPSDisplay statusBarGPSDisplay(GPSState gpsState, RecordingState recordingState);

  // Dependencies (injected)
  DisplayHardware& hardware_;
  TouchInput& touch_;
  Scheduler& scheduler_;

  // Callbacks
  GetGPSStateFn getGPSState_;

  // State
  ScreenState screenState_ = ScreenState::MAIN_MENU;
  DisplaySleepState sleepState_ = DisplaySleepState::AWAKE;

  // Screen registry
  static constexpr int NUM_SCREENS = 6;
  std::array<Screen*, NUM_SCREENS> screens_{};

  // Hardware
  StatusBar statusBar_;

  // GPS data for display tick. Written by the app-controller task
  // (updateGPSData) and read by the GPS-display task (gpsDisplayTick) — guard
  // the struct + flag with a mutex so a ~64 B GPSData can't be read half-updated
  // across cores (B6).
  GPSData pendingGPSData_{};
  bool hasPendingGPSData_ = false;
  std::mutex gpsDataMutex_;

  // Touch sleep timer
  uint32_t lastActivityTime_ = 0;
  static constexpr uint32_t SLEEP_TIMEOUT_MS = 30000;

  // GPS display throttle (initialized so first update passes immediately)
  uint32_t lastGPSDisplayUpdate_ = 0 - GPS_DISPLAY_INTERVAL_MS;
  static constexpr uint32_t GPS_DISPLAY_INTERVAL_MS = 1000;
};
