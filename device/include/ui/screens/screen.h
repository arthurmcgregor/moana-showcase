#pragma once

#include "hardware/gps_types.h"
#include "storage/sync_types.h"
#include "types.h"

class ScreenContext;

// Base class for all screens. Each screen declares its own data needs and
// policies via virtual methods. Display dispatches polymorphically — no switch
// statements, no need to know which concrete screen is active.
class Screen {
 public:
  virtual ~Screen() = default;

  virtual void init(ScreenContext* ctx) = 0;
  virtual void render() = 0;
  // Fired once when the screen becomes active via a transition (Display::show),
  // after the first paint. Unlike render() it is NOT called on display wake or
  // periodic redraw, so it's the place for one-shot entry side effects (e.g.
  // kicking off a WiFi connect) rather than pure painting.
  virtual void onEnter() {}
  virtual void handleClick(int x, int y) {}
  virtual void handleHold(int x, int y) {}

  // Screens declare their own data needs and policies
  virtual bool wantsGPSData() const { return false; }
  virtual void updateGPSData(const GPSData& data) { (void)data; }
  virtual bool keepAwake() const { return false; }
  virtual bool canEnter(GPSState gpsState) const { (void)gpsState; return true; }

  // Periodic redraw hook. Called from the existing GPS display task cadence
  // (~10 Hz) when wantsTick() returns true. Use for screens whose content
  // animates from internal subsystem state (e.g. calibration countdown).
  virtual bool wantsTick() const { return false; }
  virtual void tick() {}

  // Optional state hooks — override only in screens that care
  virtual void onWifiStateChanged(WifiState state) { (void)state; }
  virtual void onSyncStateChanged(SyncState state, int trackCount) { (void)state; (void)trackCount; }
  virtual void onSyncResult(const SyncResult& result) { (void)result; }
  // Sync was requested but couldn't start (SD mount busy with a web archive op).
  virtual void onSyncDeferred() {}
  virtual void onStatusChanged(GPSState gps, RecordingState rec) { (void)gps; (void)rec; }
};
