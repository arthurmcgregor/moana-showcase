#pragma once

#include "hardware/imu.h"
#include "ui/screens/screen.h"
#include "ui/screens/screen_shared.h"
#include "types.h"

class ScreenContext;

// Calibration UX for the magnetometer (issue #66). Reads progress directly
// from the IMU subsystem each tick rather than going via events — calibration
// is a foreground modal operation, not part of the broader event flow.
class CalibrationScreen : public Screen {
 public:
  explicit CalibrationScreen(const IMU& imu);

  void init(ScreenContext* ctx) override;
  void render() override;
  void handleClick(int x, int y) override;
  void handleHold(int x, int y) override;

  void onStatusChanged(GPSState gps, RecordingState rec) override;

  bool wantsTick() const override;
  void tick() override;

 private:
  // What's currently on the screen. Tracked so tick() can detect when the
  // backing IMU state has moved and trigger a full redraw rather than
  // overpainting incrementally.
  enum class View {
    UNKNOWN,
    BLOCKED_RECORDING,  // start gated by recording in progress
    READY,              // FUSING, not blocked — "Start" button
    RUNNING,            // CALIBRATING — countdown + ranges + "Cancel"
    RESULT_OK,          // CAL_RESULT_PENDING, valid — "Apply" + "Discard"
    RESULT_BAD,         // CAL_RESULT_PENDING, too few samples / degenerate
  };

  View desiredView() const;
  void renderView(View v);
  void renderRunning(const CalProgress& p);
  void renderResult(const CalResult& r);
  void drawTwoButtons(const char* leftLabel, ButtonColor leftColor,
                      const char* rightLabel, ButtonColor rightColor);

  const IMU& imu_;
  ScreenContext* ctx_ = nullptr;
  View currentView_ = View::UNKNOWN;
  RecordingState recordingState_ = RecordingState::IDLE;

  // Cached values so the running-screen tick only redraws when something
  // changed (cuts SPI traffic ~3×).
  uint32_t lastDrawnSecond_ = UINT32_MAX;
  uint32_t lastDrawnSamples_ = UINT32_MAX;
};
