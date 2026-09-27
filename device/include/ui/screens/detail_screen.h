#pragma once

#include "hardware/gps.h"
#include "types.h"
#include "ui/screens/screen.h"

class DetailScreen : public Screen {
 public:
  DetailScreen();
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

  // R8: on LOST, repaint the last-known fix dimmed with a "NO FIX" caption.
  // No new fix arrives while LOST, so the cached values are all we can show.
  void onStatusChanged(GPSState gps, RecordingState rec) override;

 private:
  ScreenContext* ctx_ = nullptr;
  float current_speed;
  GPSState gpsState_ = GPSState::LOCKED;
  GPSData lastData_{};
  bool haveData_ = false;
  void drawDetails(const GPSData& data);
};
