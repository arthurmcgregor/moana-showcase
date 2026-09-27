#include <gtest/gtest.h>

#include "ui/display.h"
#include "fakes/fake_display_hardware.h"
#include "fakes/fake_touch_input.h"
#include "fakes/fake_scheduler.h"
#include "fakes/fake_screen.h"

class DisplayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    display = std::make_unique<Display>(hw, touch, sched);

    mainMenu.canEnterResult = true;
    speedScreen.canEnterResult = true;
    speedScreen.wantsGPS = true;
    speedScreen.stayAwake = true;

    display->registerScreen(ScreenState::MAIN_MENU, &mainMenu);
    display->registerScreen(ScreenState::SPEED_SCREEN, &speedScreen);
    display->registerScreen(ScreenState::DETAIL_SCREEN, &detailScreen);
    display->registerScreen(ScreenState::WIFI_SCREEN, &wifiScreen);
    display->registerScreen(ScreenState::SYNC_SCREEN, &syncScreen);
    display->registerScreen(ScreenState::CALIBRATION_SCREEN, &calScreen);

    display->init(&screenContext, [this]() { return currentGPSState; });
  }

  FakeDisplayHardware hw;
  FakeTouchInput touch;
  FakeScheduler sched;
  std::unique_ptr<Display> display;
  ScreenContext screenContext;

  FakeScreen mainMenu;
  FakeScreen speedScreen;
  FakeScreen detailScreen;
  FakeScreen wifiScreen;
  FakeScreen syncScreen;
  FakeScreen calScreen;

  GPSState currentGPSState = GPSState::LOCKED;
};

// ============================================
// Screen Transitions
// ============================================

TEST_F(DisplayTest, StartsOnMainMenu) {
  EXPECT_EQ(display->getScreenState(), ScreenState::MAIN_MENU);
}

TEST_F(DisplayTest, ShowScreenTransitions) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  EXPECT_EQ(display->getScreenState(), ScreenState::SPEED_SCREEN);
  EXPECT_EQ(speedScreen.renderCount, 1);
}

TEST_F(DisplayTest, ShowScreenBlockedByCanEnter) {
  speedScreen.canEnterResult = false;
  display->showScreen(ScreenState::SPEED_SCREEN);
  EXPECT_EQ(display->getScreenState(), ScreenState::MAIN_MENU);
  EXPECT_EQ(speedScreen.renderCount, 0);
}

TEST_F(DisplayTest, ShowScreenNoOpForSameScreen) {
  int rendersBefore = mainMenu.renderCount;
  display->showScreen(ScreenState::MAIN_MENU);
  EXPECT_EQ(mainMenu.renderCount, rendersBefore);
}

// ============================================
// Sleep/Wake
// ============================================

TEST_F(DisplayTest, SleepsAfterTimeout) {
  sched.setTime(30001);
  display->touchTick();
  EXPECT_EQ(display->getDisplaySleepState(), DisplaySleepState::SLEEPING);
  EXPECT_TRUE(hw.sleeping);
}

TEST_F(DisplayTest, KeepAwakeScreenPreventsAutoSleep) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  sched.setTime(60000);
  display->touchTick();
  EXPECT_EQ(display->getDisplaySleepState(), DisplaySleepState::AWAKE);
}

TEST_F(DisplayTest, WakesOnTouch) {
  display->sleep();
  EXPECT_EQ(display->getDisplaySleepState(), DisplaySleepState::SLEEPING);

  touch.injectClick(100, 100);
  display->touchTick();
  EXPECT_EQ(display->getDisplaySleepState(), DisplaySleepState::AWAKE);
}

TEST_F(DisplayTest, TouchWhileSleepingDoesNotDispatchToScreen) {
  display->sleep();
  touch.injectClick(100, 100);
  display->touchTick();
  EXPECT_EQ(mainMenu.clickCount, 0);  // wake only, no click dispatch
}

// ============================================
// Touch Dispatch
// ============================================

TEST_F(DisplayTest, ClickDispatchesToActiveScreen) {
  touch.injectClick(50, 60);
  display->touchTick();
  EXPECT_EQ(mainMenu.clickCount, 1);
  EXPECT_EQ(mainMenu.lastClickX, 50);
  EXPECT_EQ(mainMenu.lastClickY, 60);
}

TEST_F(DisplayTest, HoldDispatchesToActiveScreen) {
  touch.injectHold(70, 80);
  display->touchTick();
  EXPECT_EQ(mainMenu.holdCount, 1);
  EXPECT_EQ(mainMenu.lastHoldX, 70);
  EXPECT_EQ(mainMenu.lastHoldY, 80);
}

// B5: touch handlers draw directly, so they must run under the SPI lock — not
// concurrently with a status-bar repaint on another task.
TEST_F(DisplayTest, ClickDispatchRunsUnderSPILock) {
  mainMenu.spiDepthProbe = [this]() { return hw.lockCount - hw.unlockCount; };
  touch.injectClick(50, 60);
  display->touchTick();
  EXPECT_GT(mainMenu.spiDepthAtClick, 0);   // handler ran while the lock was held
  EXPECT_EQ(hw.lockCount, hw.unlockCount);  // and it was released again
}

TEST_F(DisplayTest, HoldDispatchRunsUnderSPILock) {
  mainMenu.spiDepthProbe = [this]() { return hw.lockCount - hw.unlockCount; };
  touch.injectHold(70, 80);
  display->touchTick();
  EXPECT_GT(mainMenu.spiDepthAtHold, 0);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, TouchDispatchesToCorrectScreenAfterTransition) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  touch.injectClick(50, 60);
  display->touchTick();
  EXPECT_EQ(speedScreen.clickCount, 1);
  EXPECT_EQ(mainMenu.clickCount, 0);
}

// ============================================
// GPS Data Routing
// ============================================

TEST_F(DisplayTest, GPSDataSkippedOnMainMenu) {
  GPSData data{};
  data.speed = 5.5;
  display->updateGPSData(data);
  display->gpsDisplayTick();
  EXPECT_EQ(mainMenu.gpsUpdateCount, 0);  // mainMenu.wantsGPS = false
}

TEST_F(DisplayTest, GPSDataRoutedToSpeedScreen) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  GPSData data{};
  data.speed = 5.5;
  display->updateGPSData(data);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);
  EXPECT_DOUBLE_EQ(speedScreen.lastGPSData.speed, 5.5);
}

TEST_F(DisplayTest, GPSDataThrottledTo1Hz) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  GPSData data{};

  data.speed = 1.0;
  display->updateGPSData(data);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);

  // Immediate second update should be throttled
  sched.advanceTime(500);
  data.speed = 2.0;
  display->updateGPSData(data);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);

  // After 1 second, update should go through
  sched.advanceTime(600);
  data.speed = 3.0;
  display->updateGPSData(data);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 2);
}

TEST_F(DisplayTest, GPSDataSkippedWhileSleeping) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  display->sleep();

  GPSData data{};
  data.speed = 5.0;
  display->updateGPSData(data);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 0);
}

// ============================================
// State Updates
// ============================================

TEST_F(DisplayTest, WifiStateRoutedToActiveScreen) {
  display->showScreen(ScreenState::WIFI_SCREEN);
  display->updateWifi(WifiState::CONNECTED);
  EXPECT_EQ(wifiScreen.wifiUpdateCount, 1);
  EXPECT_EQ(wifiScreen.lastWifiState, WifiState::CONNECTED);
}

// B12: state hooks broadcast to every registered screen, not just the active
// one, so an inactive screen keeps its cache current and renders correctly on
// re-entry. (Inactive screens guard their own drawing on ctx_->current().)
TEST_F(DisplayTest, WifiStateBroadcastToInactiveScreens) {
  display->showScreen(ScreenState::MAIN_MENU);
  display->updateWifi(WifiState::CONNECTED);
  EXPECT_EQ(mainMenu.wifiUpdateCount, 1);  // active screen
  EXPECT_EQ(wifiScreen.wifiUpdateCount, 1);  // inactive, still notified
  EXPECT_EQ(wifiScreen.lastWifiState, WifiState::CONNECTED);
}

TEST_F(DisplayTest, SyncStateRoutedToActiveScreen) {
  display->showScreen(ScreenState::SYNC_SCREEN);
  display->updateSync(SyncState::SYNCING, 5);
  EXPECT_EQ(syncScreen.syncUpdateCount, 1);
  EXPECT_EQ(syncScreen.lastSyncState, SyncState::SYNCING);
  EXPECT_EQ(syncScreen.lastTrackCount, 5);
}

TEST_F(DisplayTest, SyncStateBroadcastToInactiveScreens) {
  display->showScreen(ScreenState::MAIN_MENU);
  display->updateSync(SyncState::SYNCING, 5);
  EXPECT_EQ(syncScreen.syncUpdateCount, 1);  // inactive, still notified
  EXPECT_EQ(syncScreen.lastSyncState, SyncState::SYNCING);
  EXPECT_EQ(syncScreen.lastTrackCount, 5);
}

TEST_F(DisplayTest, SyncResultBroadcastToInactiveScreens) {
  display->showScreen(ScreenState::MAIN_MENU);
  SyncResult result{};
  result.filesSucceeded = 3;
  display->showSyncResult(result);
  EXPECT_EQ(syncScreen.syncResultCount, 1);  // inactive, still notified
  EXPECT_EQ(syncScreen.lastSyncResult.filesSucceeded, 3);
}

TEST_F(DisplayTest, SyncDeferredBroadcastToInactiveScreens) {
  display->showScreen(ScreenState::MAIN_MENU);
  display->showSyncDeferred();
  EXPECT_EQ(syncScreen.syncDeferredCount, 1);  // inactive, still notified
}

TEST_F(DisplayTest, StatusBarUpdateRoutesToActiveScreen) {
  display->updateStatusBar(GPSState::LOCKED, RecordingState::ACTIVE);
  EXPECT_EQ(mainMenu.statusUpdateCount, 1);
  EXPECT_EQ(mainMenu.lastGPSState, GPSState::LOCKED);
  EXPECT_EQ(mainMenu.lastRecState, RecordingState::ACTIVE);
}

// ============================================
// SPI Locking
// ============================================

TEST_F(DisplayTest, ShowScreenAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->showScreen(ScreenState::SPEED_SCREEN);
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, AcquireSPIReturnsGuard) {
  int locksBefore = hw.lockCount;
  {
    auto guard = display->acquireSPI();
    EXPECT_EQ(hw.lockCount, locksBefore + 1);
  }
  EXPECT_EQ(hw.unlockCount, hw.lockCount);
}

// ============================================
// Wake Re-renders
// ============================================

TEST_F(DisplayTest, WakeReRendersCurrentScreen) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  int rendersBefore = speedScreen.renderCount;
  display->sleep();
  display->wake();
  EXPECT_EQ(speedScreen.renderCount, rendersBefore + 1);
}

TEST_F(DisplayTest, WakeIsNoOpWhenAlreadyAwake) {
  int rendersBefore = mainMenu.renderCount;
  display->wake();
  EXPECT_EQ(mainMenu.renderCount, rendersBefore);
}

// ============================================
// Sleep Edge Cases
// ============================================

TEST_F(DisplayTest, SleepIsNoOpWhenAlreadySleeping) {
  display->sleep();
  EXPECT_EQ(hw.sleepCallCount, 1);
  display->sleep();
  EXPECT_EQ(hw.sleepCallCount, 1);
}

TEST_F(DisplayTest, TouchResetsActivityTimer) {
  sched.setTime(20000);
  touch.injectClick(50, 50);
  display->touchTick();

  // 29 seconds after the touch — should still be awake
  sched.setTime(49000);
  display->touchTick();
  EXPECT_EQ(display->getDisplaySleepState(), DisplaySleepState::AWAKE);

  // 31 seconds after the touch — should sleep
  sched.setTime(50001);
  display->touchTick();
  EXPECT_EQ(display->getDisplaySleepState(), DisplaySleepState::SLEEPING);
}

// ============================================
// Initialization
// ============================================

TEST_F(DisplayTest, InitCallsInitOnAllScreens) {
  EXPECT_TRUE(mainMenu.initCalled);
  EXPECT_TRUE(speedScreen.initCalled);
  EXPECT_TRUE(detailScreen.initCalled);
  EXPECT_TRUE(wifiScreen.initCalled);
  EXPECT_TRUE(syncScreen.initCalled);
  EXPECT_TRUE(calScreen.initCalled);
}

TEST_F(DisplayTest, InitRendersInitialScreen) {
  // init() renders the main menu screen (the initial screen)
  EXPECT_GE(mainMenu.renderCount, 1);
}

// ============================================
// SPI Discipline
// ============================================

TEST_F(DisplayTest, UpdateStatusBarAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->updateStatusBar(GPSState::LOCKED, RecordingState::ACTIVE);
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, UpdateWifiAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->updateWifi(WifiState::CONNECTED);
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, UpdateSyncAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->updateSync(SyncState::SYNCING, 3);
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, SleepAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->sleep();
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, GPSDisplayTickAcquiresSPI) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  GPSData data{};
  display->updateGPSData(data);
  int locksBefore = hw.lockCount;
  display->gpsDisplayTick();
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

// ============================================
// Full-Screen Messages
// ============================================

TEST_F(DisplayTest, ShowRecoveryMessageAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->showRecoveryMessage();
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, ShowFatalErrorAcquiresSPI) {
  int locksBefore = hw.lockCount;
  display->showFatalError("test error");
  EXPECT_GT(hw.lockCount, locksBefore);
  EXPECT_EQ(hw.lockCount, hw.unlockCount);
}

TEST_F(DisplayTest, ShowFatalErrorDrawsMessage) {
  display->showFatalError("DISK FULL");
  EXPECT_STREQ(hw.lastPrintln, "DISK FULL");
}

// ============================================
// GPS Data Edge Cases
// ============================================

TEST_F(DisplayTest, GPSDisplayTickNoOpWithoutPendingData) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  int locksBefore = hw.lockCount;
  display->gpsDisplayTick();
  EXPECT_EQ(hw.lockCount, locksBefore);  // no SPI acquired
  EXPECT_EQ(speedScreen.gpsUpdateCount, 0);
}

TEST_F(DisplayTest, PendingGPSDataNotDispatchedAfterScreenSwitch) {
  display->showScreen(ScreenState::SPEED_SCREEN);
  GPSData data{};
  data.speed = 5.0;
  display->updateGPSData(data);

  // Switch to main menu before tick processes the data
  display->showScreen(ScreenState::MAIN_MENU);
  display->gpsDisplayTick();

  // Main menu doesn't want GPS data — should not dispatch
  EXPECT_EQ(mainMenu.gpsUpdateCount, 0);
}

// ============================================
// canEnter Uses Current GPS State
// ============================================

TEST_F(DisplayTest, CanEnterUsesCurrentGPSState) {
  // Start with GPS locked — speed screen is accessible
  currentGPSState = GPSState::LOCKED;
  speedScreen.canEnterResult = true;
  display->showScreen(ScreenState::SPEED_SCREEN);
  EXPECT_EQ(display->getScreenState(), ScreenState::SPEED_SCREEN);

  // Go back to main menu
  display->showScreen(ScreenState::MAIN_MENU);

  // Now GPS is acquiring — speed screen should be blocked
  speedScreen.canEnterResult = false;
  display->showScreen(ScreenState::SPEED_SCREEN);
  EXPECT_EQ(display->getScreenState(), ScreenState::MAIN_MENU);
}

// ============================================
// B6 — pendingGPSData_ snapshot consistency
// ============================================

// The reader delivers one whole fix, never a mix of two writes. A unit test
// can't force the cross-core interleave, but this guards the snapshot contract:
// the delivered struct's fields all come from the latest complete write.
TEST_F(DisplayTest, GPSDataSnapshotReflectsLatestCompleteWrite) {
  display->showScreen(ScreenState::SPEED_SCREEN);

  GPSData first{};
  first.lat = 1.0;
  first.lng = 2.0;
  first.speed = 3.0;
  display->updateGPSData(first);

  GPSData second{};
  second.lat = 10.0;
  second.lng = 20.0;
  second.speed = 30.0;
  display->updateGPSData(second);

  display->gpsDisplayTick();

  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);
  EXPECT_DOUBLE_EQ(speedScreen.lastGPSData.lat, 10.0);
  EXPECT_DOUBLE_EQ(speedScreen.lastGPSData.lng, 20.0);
  EXPECT_DOUBLE_EQ(speedScreen.lastGPSData.speed, 30.0);
}

// A throttled tick must leave the pending fix intact for a later tick (the
// snapshot lock must not consume-then-drop it).
TEST_F(DisplayTest, ThrottledTickRetainsPendingData) {
  display->showScreen(ScreenState::SPEED_SCREEN);

  GPSData a{};
  a.speed = 1.0;
  display->updateGPSData(a);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);

  // Within the throttle window: update is held, not delivered, not lost.
  sched.advanceTime(200);
  GPSData b{};
  b.speed = 2.0;
  display->updateGPSData(b);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);

  // Past the window: the retained fix is delivered.
  sched.advanceTime(1000);
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 2);
  EXPECT_DOUBLE_EQ(speedScreen.lastGPSData.speed, 2.0);
}

// ============================================
// QW13 — DisplayCmd dispatch (apply())
//
// AppController issues every render-side instruction as a DisplayCmd through
// the single apply() entry point. These exercise each Kind end-to-end so the
// tagged-union dispatch can't silently route a command to the wrong handler.
// ============================================

TEST_F(DisplayTest, ApplyShowScreenTransitions) {
  display->apply(DisplayCmd::showScreen(ScreenState::SPEED_SCREEN));
  EXPECT_EQ(display->getScreenState(), ScreenState::SPEED_SCREEN);
}

TEST_F(DisplayTest, ApplyUpdateStatusBarRoutesToActiveScreen) {
  display->apply(DisplayCmd::updateStatusBar(GPSState::LOCKED, RecordingState::ACTIVE));
  EXPECT_EQ(mainMenu.statusUpdateCount, 1);
  EXPECT_EQ(mainMenu.lastGPSState, GPSState::LOCKED);
  EXPECT_EQ(mainMenu.lastRecState, RecordingState::ACTIVE);
}

TEST_F(DisplayTest, ApplyUpdateGPSDataRoutesToSpeedScreen) {
  display->apply(DisplayCmd::showScreen(ScreenState::SPEED_SCREEN));
  GPSData data{};
  data.speed = 7.5;
  display->apply(DisplayCmd::updateGPSData(data));
  display->gpsDisplayTick();
  EXPECT_EQ(speedScreen.gpsUpdateCount, 1);
  EXPECT_DOUBLE_EQ(speedScreen.lastGPSData.speed, 7.5);
}

TEST_F(DisplayTest, ApplyUpdateSyncRoutesToSyncScreen) {
  display->apply(DisplayCmd::updateSync(SyncState::SYNCING, 4));
  EXPECT_EQ(syncScreen.syncUpdateCount, 1);
  EXPECT_EQ(syncScreen.lastSyncState, SyncState::SYNCING);
  EXPECT_EQ(syncScreen.lastTrackCount, 4);
}

TEST_F(DisplayTest, ApplyShowSyncResultRoutesToSyncScreen) {
  SyncResult result{};
  result.filesSucceeded = 2;
  display->apply(DisplayCmd::showSyncResult(result));
  EXPECT_EQ(syncScreen.syncResultCount, 1);
  EXPECT_EQ(syncScreen.lastSyncResult.filesSucceeded, 2);
}

TEST_F(DisplayTest, ApplyShowSyncDeferredRoutesToSyncScreen) {
  display->apply(DisplayCmd::showSyncDeferred());
  EXPECT_EQ(syncScreen.syncDeferredCount, 1);
}

TEST_F(DisplayTest, ApplyShowFatalErrorDrawsMessage) {
  display->apply(DisplayCmd::showFatalError("APPLY BOOM"));
  EXPECT_STREQ(hw.lastPrintln, "APPLY BOOM");
}
