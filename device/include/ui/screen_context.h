#pragma once

// ScreenContext — the only collaborator a screen receives.
//
// Navigation (show/back/current) calls Display directly on the touch task;
// Display takes the SPI lock internally so this is safe.
//
// Commands (toggleRecording/toggleWifi/requestSync/format) post BUTTON_PRESS
// events onto the FreeRTOS queue so AppController retains state-machine
// ownership (boot recovery, sync guards, etc.).

#include <functional>

#include "types.h"

class Display;

class ScreenContext {
 public:
  using TrackCountFn = std::function<int()>;

  ScreenContext() = default;

  // Wired once during App::init, after Display is constructed.
  void init(Display* display, PostEventFn post, TrackCountFn trackCount);

  // ---- Navigation (sync, runs on the touch task) ----
  void show(ScreenState s);
  void back();
  ScreenState current() const;

  // ---- Commands (async, posted to the event queue) ----
  void toggleRecording();
  void toggleWifi();
  void requestSync();
  void format();
  void startCalibration();
  void cancelCalibration();
  void applyCalibration();
  void discardCalibration();

  // ---- Read-only state ----
  int localTrackCount() const { return trackCount_(); }
  // Current GPS fix state, so a screen can reflect staleness on entry (not
  // just on the next transition event). Runs on the touch task.
  GPSState gpsState() const;

 private:
  Display* display_ = nullptr;
  PostEventFn post_;
  TrackCountFn trackCount_;
};
