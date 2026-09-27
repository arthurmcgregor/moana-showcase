#include "ui/display.h"

#include "utils/logger.h"
#include "utils/state_strings.h"

static const char* LOG_TAG = "Display";

Display::Display(DisplayHardware& hardware, TouchInput& touch, Scheduler& scheduler)
    : hardware_(hardware), touch_(touch), scheduler_(scheduler) {}

void Display::init(ScreenContext* context, GetGPSStateFn getGPSState) {
  getGPSState_ = std::move(getGPSState);

  lastActivityTime_ = scheduler_.millis();

  // Initialize all registered screens
  for (auto* screen : screens_) {
    if (screen) {
      screen->init(context);
    }
  }

  hardware_.lockSPI();
  statusBar_.render();
  statusBar_.updateBattery(hardware_.getBatteryLevel());
  if (auto* screen = activeScreen()) {
    screen->render();
  }
  hardware_.unlockSPI();
}

// ============================================
// Screen Registration
// ============================================

void Display::registerScreen(ScreenState id, Screen* screen) {
  screens_[static_cast<int>(id)] = screen;
}

// ============================================
// Render-Side Command Dispatch
// ============================================

void Display::apply(const DisplayCmd& cmd) {
  switch (cmd.kind) {
    case DisplayCmd::Kind::SHOW_SCREEN:
      showScreen(cmd.data.screen);
      break;
    case DisplayCmd::Kind::UPDATE_STATUS_BAR:
      updateStatusBar(cmd.data.statusBar.gps, cmd.data.statusBar.recording);
      break;
    case DisplayCmd::Kind::UPDATE_GPS_DATA:
      updateGPSData(cmd.data.gpsData);
      break;
    case DisplayCmd::Kind::UPDATE_SYNC:
      updateSync(cmd.data.sync.state, cmd.data.sync.trackCount);
      break;
    case DisplayCmd::Kind::SHOW_SYNC_RESULT:
      showSyncResult(cmd.data.syncResult);
      break;
    case DisplayCmd::Kind::SHOW_SYNC_DEFERRED:
      showSyncDeferred();
      break;
    case DisplayCmd::Kind::SHOW_FATAL_ERROR:
      showFatalError(cmd.data.fatalMessage);
      break;
  }
}

// ============================================
// Screen Management
// ============================================

Screen* Display::activeScreen() const {
  return screens_[static_cast<int>(screenState_)];
}

bool Display::canTransitionToScreen(ScreenState newState) const {
  auto* target = screens_[static_cast<int>(newState)];
  return target && target->canEnter(getGPSState_());
}

void Display::showScreen(ScreenState newState) {
  if (screenState_ == newState) {
    return;
  }

  if (!canTransitionToScreen(newState)) {
    LOG_WARN(LOG_TAG, "Invalid screen transition: %s -> %s", screenStateToString(screenState_),
             screenStateToString(newState));
    return;
  }

  LOG_INFO(LOG_TAG, "Screen transition: %s -> %s", screenStateToString(screenState_),
           screenStateToString(newState));

  screenState_ = newState;
  hardware_.lockSPI();
  if (auto* screen = activeScreen()) {
    screen->render();
  }
  hardware_.unlockSPI();

  // One-shot entry hook, after the first paint. Kept outside the SPI lock:
  // onEnter() drives commands (posting events), not display writes.
  if (auto* screen = activeScreen()) {
    screen->onEnter();
  }
}

// ============================================
// State Updates
// ============================================

void Display::updateStatusBar(GPSState gpsState, RecordingState recordingState) {
  hardware_.lockSPI();
  statusBar_.updateGPS(statusBarGPSDisplay(gpsState, recordingState));
  if (auto* screen = activeScreen()) {
    screen->onStatusChanged(gpsState, recordingState);
  }
  hardware_.unlockSPI();
}

void Display::updateGPSData(const GPSData& data) {
  if (auto* screen = activeScreen(); screen && screen->wantsGPSData()) {
    std::lock_guard<std::mutex> lock(gpsDataMutex_);
    pendingGPSData_ = data;
    hasPendingGPSData_ = true;
  }
}

// Sync/WiFi state hooks broadcast to *every* registered screen, not just the
// active one (B12). A screen the user navigated away from mid-operation must
// still update its cached state so it renders correctly on re-entry; otherwise
// it stays frozen at the state it held when it lost focus (e.g. SyncScreen
// stuck showing "Syncing…" with a stale track count). Inactive screens only
// cache — each guards its own drawing on ctx_->current() — so broadcasting
// under the SPI lock stays cheap and paints nothing off-screen.
void Display::updateWifi(WifiState state) {
  hardware_.lockSPI();
  statusBar_.updateWifi(state == WifiState::CONNECTED ? WifiDisplay::Connected
                                                      : WifiDisplay::Disconnected);
  for (auto* screen : screens_) {
    if (screen) screen->onWifiStateChanged(state);
  }
  hardware_.unlockSPI();
}

void Display::updateSync(SyncState state, int trackCount) {
  hardware_.lockSPI();
  for (auto* screen : screens_) {
    if (screen) screen->onSyncStateChanged(state, trackCount);
  }
  hardware_.unlockSPI();
}

void Display::showSyncResult(const SyncResult& result) {
  hardware_.lockSPI();
  for (auto* screen : screens_) {
    if (screen) screen->onSyncResult(result);
  }
  hardware_.unlockSPI();
}

void Display::showSyncDeferred() {
  hardware_.lockSPI();
  for (auto* screen : screens_) {
    if (screen) screen->onSyncDeferred();
  }
  hardware_.unlockSPI();
}

// ============================================
// Status Bar Updates
// ============================================

void Display::updateBattery(int level) {
  hardware_.lockSPI();
  statusBar_.updateBattery(level);
  hardware_.unlockSPI();
}

void Display::updateTime(uint8_t hours, uint8_t minutes, uint8_t seconds) {
  m5::rtc_time_t time;
  time.hours = hours;
  time.minutes = minutes;
  time.seconds = seconds;
  hardware_.lockSPI();
  statusBar_.updateTime(time);
  hardware_.unlockSPI();
}

// ============================================
// Display Sleep/Wake
// ============================================

void Display::sleep() {
  if (sleepState_ == DisplaySleepState::SLEEPING) {
    return;
  }

  LOG_INFO(LOG_TAG, "Putting display to sleep");
  sleepState_ = DisplaySleepState::SLEEPING;
  hardware_.lockSPI();
  hardware_.displaySleep();
  hardware_.unlockSPI();
}

void Display::wake() {
  if (sleepState_ == DisplaySleepState::AWAKE) {
    return;
  }

  LOG_INFO(LOG_TAG, "Waking display");
  sleepState_ = DisplaySleepState::AWAKE;
  hardware_.lockSPI();
  hardware_.displayWakeup();
  hardware_.setBrightness(255);
  statusBar_.render();
  if (auto* screen = activeScreen()) {
    screen->render();
  }
  hardware_.unlockSPI();
}

// ============================================
// Full-Screen Messages
// ============================================

void Display::showRecoveryMessage() {
  hardware_.lockSPI();
  hardware_.fillRect(0, 30, hardware_.width(), hardware_.height() - 30, 0x0000);  // TFT_BLACK
  hardware_.setTextStyle(ui::TextStyle::Body);
  hardware_.setTextColor(0xFFE0);  // TFT_YELLOW
  hardware_.setTextDatum(1);       // top_center
  hardware_.drawString("Filesystem corrupted!", hardware_.width() / 2, 60);
  hardware_.drawString("Syncing data to SD...", hardware_.width() / 2, 90);
  hardware_.unlockSPI();
}

void Display::showFatalError(const char* message) {
  hardware_.lockSPI();
  hardware_.fillScreen(0xF800);  // TFT_RED
  hardware_.setTextColor(0xFFFF);  // TFT_WHITE
  hardware_.setTextStyle(ui::TextStyle::Body);
  hardware_.setCursor(10, 100);
  hardware_.println(message);
  hardware_.unlockSPI();
}

// ============================================
// SPI Lock
// ============================================

Display::SPIGuard Display::acquireSPI() {
  return SPIGuard(hardware_);
}

// ============================================
// Display Helpers
// ============================================

GPSDisplay Display::statusBarGPSDisplay(GPSState gpsState, RecordingState recordingState) {
  switch (gpsState) {
    case GPSState::ACQUIRING:
      return GPSDisplay::Acquiring;
    case GPSState::LOCKED:
      switch (recordingState) {
        case RecordingState::ACTIVE:
          return GPSDisplay::Recording;
        case RecordingState::IDLE:
          return GPSDisplay::Idle;
        case RecordingState::STARTING:
        case RecordingState::STOPPING:
          return GPSDisplay::Busy;
        default:
          return GPSDisplay::Error;
      }
    default:
      return GPSDisplay::Error;
  }
}

// ============================================
// Tick Functions
// ============================================

void Display::touchTick() {
  touch_.update();
  TouchEvent event = touch_.poll();

  if (event.type != TouchEvent::NONE) {
    if (sleepState_ == DisplaySleepState::SLEEPING) {
      LOG_INFO(LOG_TAG, "Touch detected - waking display");
      wake();
      lastActivityTime_ = scheduler_.millis();
    } else {
      lastActivityTime_ = scheduler_.millis();

      // Touch handlers draw directly (immediate feedback) and may re-enter
      // Display (a tap that navigates calls showScreen()), so take the SPI lock
      // around them — otherwise a status-bar repaint on another task drives the
      // shared LCD SPI transaction concurrently (B5). The mutex is recursive,
      // so the re-entrant showScreen()/wake() lock is safe.
      if (event.type == TouchEvent::HOLD) {
        LOG_INFO(LOG_TAG, "Hold at (%d, %d)", event.x, event.y);
        if (auto* screen = activeScreen()) {
          hardware_.lockSPI();
          screen->handleHold(event.x, event.y);
          hardware_.unlockSPI();
        }
      } else if (event.type == TouchEvent::CLICK) {
        LOG_INFO(LOG_TAG, "Click at (%d, %d)", event.x, event.y);
        if (auto* screen = activeScreen()) {
          hardware_.lockSPI();
          screen->handleClick(event.x, event.y);
          hardware_.unlockSPI();
        }
      }
    }
  }

  auto* screen = activeScreen();
  bool shouldStayAwake = screen && screen->keepAwake();

  if (!shouldStayAwake && sleepState_ == DisplaySleepState::AWAKE &&
      (scheduler_.millis() - lastActivityTime_) >= SLEEP_TIMEOUT_MS) {
    LOG_INFO(LOG_TAG, "Display sleep timeout reached on %s", screenStateToString(screenState_));
    sleep();
    lastActivityTime_ = scheduler_.millis();
  }
}

void Display::gpsDisplayTick() {
  if (sleepState_ != DisplaySleepState::AWAKE) {
    return;
  }

  auto* screen = activeScreen();
  if (!screen) return;

  if (screen->wantsTick()) {
    hardware_.lockSPI();
    screen->tick();
    hardware_.unlockSPI();
  }

  // Snapshot the pending fix under the data lock, then draw outside it. The
  // throttle check stays inside so a throttled tick leaves the data pending for
  // a later tick (unchanged behaviour), but the copy can't tear (B6). The SPI
  // lock is never held while holding the data lock.
  GPSData snapshot;
  {
    std::lock_guard<std::mutex> lock(gpsDataMutex_);
    if (!hasPendingGPSData_) return;

    uint32_t now = scheduler_.millis();
    if (now - lastGPSDisplayUpdate_ < GPS_DISPLAY_INTERVAL_MS) return;

    snapshot = pendingGPSData_;
    hasPendingGPSData_ = false;
    lastGPSDisplayUpdate_ = now;
  }

  if (screen->wantsGPSData()) {
    hardware_.lockSPI();
    screen->updateGPSData(snapshot);
    hardware_.unlockSPI();
  }
}

// ============================================
// FreeRTOS Task Wrappers
// ============================================

#ifdef ARDUINO
void Display::touchTask(void* pvParameters) {
  LOG_INFO(LOG_TAG, "Touch monitoring task started");
  Display* display = static_cast<Display*>(pvParameters);

  for (;;) {
    display->touchTick();
    vTaskDelay(pdMS_TO_TICKS(
        display->sleepState_ == DisplaySleepState::SLEEPING ? 200 : 50));
  }
}

void Display::gpsDataTask(void* pvParameters) {
  LOG_DEBUG(LOG_TAG, "Starting GPS display task");
  Display* display = static_cast<Display*>(pvParameters);

  for (;;) {
    display->gpsDisplayTick();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
#endif
