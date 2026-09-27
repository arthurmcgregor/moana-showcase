#pragma once

#include <functional>

#include "ui/screen_context.h"
#include "ui/screens/screen.h"

// Minimal Screen stub for testing Display's coordination logic.
class FakeScreen : public Screen {
 public:
  void init(ScreenContext* ctx) override {
    ctx_ = ctx;
    initCalled = true;
  }

  void render() override { renderCount++; }
  void handleClick(int x, int y) override {
    lastClickX = x; lastClickY = y; clickCount++;
    if (spiDepthProbe) spiDepthAtClick = spiDepthProbe();
  }
  void handleHold(int x, int y) override {
    lastHoldX = x; lastHoldY = y; holdCount++;
    if (spiDepthProbe) spiDepthAtHold = spiDepthProbe();
  }

  bool wantsGPSData() const override { return wantsGPS; }
  void updateGPSData(const GPSData& data) override { lastGPSData = data; gpsUpdateCount++; }
  bool keepAwake() const override { return stayAwake; }
  bool canEnter(GPSState gpsState) const override { return canEnterResult; }

  void onWifiStateChanged(WifiState state) override { lastWifiState = state; wifiUpdateCount++; }
  void onSyncStateChanged(SyncState state, int trackCount) override {
    lastSyncState = state; lastTrackCount = trackCount; syncUpdateCount++;
  }
  void onSyncResult(const SyncResult& result) override {
    lastSyncResult = result; syncResultCount++;
  }
  void onSyncDeferred() override { syncDeferredCount++; }
  void onStatusChanged(GPSState gps, RecordingState rec) override {
    lastGPSState = gps; lastRecState = rec; statusUpdateCount++;
  }

  // Configuration
  bool wantsGPS = false;
  bool stayAwake = false;
  bool canEnterResult = true;

  // Test inspection
  bool initCalled = false;
  int renderCount = 0;
  int clickCount = 0;
  int holdCount = 0;
  int lastClickX = -1, lastClickY = -1;
  int lastHoldX = -1, lastHoldY = -1;
  // Set by a test to sample the SPI lock depth at dispatch time; the recorded
  // value is > 0 when the handler ran under the lock (B5).
  std::function<int()> spiDepthProbe;
  int spiDepthAtClick = -1;
  int spiDepthAtHold = -1;
  int gpsUpdateCount = 0;
  GPSData lastGPSData{};
  int wifiUpdateCount = 0;
  WifiState lastWifiState = WifiState::DISCONNECTED;
  int syncUpdateCount = 0;
  SyncState lastSyncState = SyncState::IDLE;
  int lastTrackCount = 0;
  int syncResultCount = 0;
  SyncResult lastSyncResult{};
  int syncDeferredCount = 0;
  int statusUpdateCount = 0;
  GPSState lastGPSState = GPSState::NOT_STARTED;
  RecordingState lastRecState = RecordingState::IDLE;
  ScreenContext* ctx_ = nullptr;
};
