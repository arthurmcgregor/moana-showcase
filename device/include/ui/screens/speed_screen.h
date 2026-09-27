#pragma once

#include "hardware/gps.h"
#include "types.h"
#include "ui/screens/screen.h"

class SpeedScreen : public Screen {
 public:
  SpeedScreen();
  void init(ScreenContext* ctx) override;
  void render() override;
  void handleClick(int x, int y) override;
  void handleHold(int x, int y) override;

  bool wantsGPSData() const override { return true; }
  void updateGPSData(const GPSData& data) override;
  bool keepAwake() const override { return true; }
  bool canEnter(GPSState gpsState) const override {
    return gpsState == GPSState::LOCKED || gpsState == GPSState::LOST;
  }

  // R8: when the fix drops to LOST while this screen is up, redraw the
  // last-known speed dimmed with a "NO FIX" caption so it isn't read as live.
  void onStatusChanged(GPSState gps, RecordingState rec) override;

 private:
  ScreenContext* ctx_ = nullptr;
  float current_speed;
  GPSState gpsState_ = GPSState::LOCKED;
  void drawSpeed(float speed);
};
