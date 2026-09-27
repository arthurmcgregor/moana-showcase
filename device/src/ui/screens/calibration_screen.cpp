#include "ui/screens/calibration_screen.h"

#include <M5Unified.h>

#include "ui/screen_context.h"
#include "ui/screens/screen_shared.h"
#include "ui/screens/screen_widgets.h"
#include "ui/text_style.h"
#include "ui/screens/touch_targets.h"
#include "utils/logger.h"

static const char* LOG_TAG = "CalibrationScreen";

namespace {

constexpr int kSingleButtonX = 20;
constexpr int kSingleButtonY = STATUS_BAR_HEIGHT + 145;
constexpr int kButtonHeight = 70;

// Calibration buttons use the shared painter at text size 2 (its labels —
// "Apply"/"Discard"/"Recalibrate" — are longer than the size-3 menu labels).
void drawFilledButton(int x, int y, int w, int h, const char* label, ButtonColor color) {
  ScreenWidgets::drawFilledButton(x, y, w, h, label, color, ui::TextStyle::Body);
}

}  // namespace

CalibrationScreen::CalibrationScreen(const IMU& imu) : imu_(imu) {}

void CalibrationScreen::init(ScreenContext* ctx) {
  LOG_INFO(LOG_TAG, "CalibrationScreen::init called");
  ctx_ = ctx;
}

CalibrationScreen::View CalibrationScreen::desiredView() const {
  switch (imu_.getMode()) {
    case IMUMode::CALIBRATING:
      return View::RUNNING;
    case IMUMode::CAL_RESULT_PENDING: {
      CalResult r = imu_.getPendingResult();
      return r.valid ? View::RESULT_OK : View::RESULT_BAD;
    }
    case IMUMode::FUSING:
    default:
      return (recordingState_ == RecordingState::IDLE) ? View::READY
                                                       : View::BLOCKED_RECORDING;
  }
}

void CalibrationScreen::render() {
  currentView_ = View::UNKNOWN;
  renderView(desiredView());
}

void CalibrationScreen::onStatusChanged(GPSState /*gps*/, RecordingState rec) {
  if (rec == recordingState_) return;
  recordingState_ = rec;
  // Only the READY / BLOCKED_RECORDING swap depends on recording state; the
  // RUNNING and RESULT views are owned by the IMU mode and survive a swap.
  View want = desiredView();
  if (want != currentView_) renderView(want);
}

bool CalibrationScreen::wantsTick() const {
  // Only RUNNING animates; in other views the IMU mode doesn't change behind
  // our back so a tick would just waste SPI traffic.
  return imu_.getMode() == IMUMode::CALIBRATING ||
         (currentView_ == View::RUNNING);  // catch the moment we transition out
}

void CalibrationScreen::tick() {
  View want = desiredView();
  if (want != currentView_) {
    renderView(want);
    return;
  }
  if (currentView_ == View::RUNNING) {
    CalProgress p = imu_.getCalProgress();
    // Cheap change-detection so the per-axis ranges flicker less.
    uint32_t remainSec = (p.elapsedMs >= 90000) ? 0 : (90000 - p.elapsedMs + 999) / 1000;
    if (remainSec == lastDrawnSecond_ && p.samples == lastDrawnSamples_) return;
    renderRunning(p);
  }
}

void CalibrationScreen::handleClick(int x, int y) {
  if (!ctx_) return;
  int w = M5.Display.width();

  switch (currentView_) {
    case View::READY: {
      int bw = w - 40;
      if (TouchTargets::isInsideButton(x, y, kSingleButtonX, kSingleButtonY, bw, kButtonHeight)) {
        LOG_INFO(LOG_TAG, "Start calibration tapped");
        ctx_->startCalibration();
      }
      break;
    }
    case View::BLOCKED_RECORDING:
      // No actionable touch. Long-hold (handleHold) still exits.
      break;
    case View::RUNNING: {
      int bw = w - 40;
      if (TouchTargets::isInsideButton(x, y, kSingleButtonX, kSingleButtonY, bw, kButtonHeight)) {
        LOG_INFO(LOG_TAG, "Cancel calibration tapped");
        ctx_->cancelCalibration();
      }
      break;
    }
    case View::RESULT_OK: {
      int half = (w - 60) / 2;
      int rightX = kSingleButtonX + half + 20;
      if (TouchTargets::isInsideButton(x, y, kSingleButtonX, kSingleButtonY, half, kButtonHeight)) {
        LOG_INFO(LOG_TAG, "Discard calibration tapped");
        ctx_->discardCalibration();
      } else if (TouchTargets::isInsideButton(x, y, rightX, kSingleButtonY, half, kButtonHeight)) {
        LOG_INFO(LOG_TAG, "Apply calibration tapped");
        ctx_->applyCalibration();
      }
      break;
    }
    case View::RESULT_BAD: {
      int bw = w - 40;
      if (TouchTargets::isInsideButton(x, y, kSingleButtonX, kSingleButtonY, bw, kButtonHeight)) {
        LOG_INFO(LOG_TAG, "Discard (bad result) tapped");
        ctx_->discardCalibration();
      }
      break;
    }
    case View::UNKNOWN:
      break;
  }
}

void CalibrationScreen::handleHold(int /*x*/, int /*y*/) {
  // Long-hold from RUNNING is the cancellation path that doesn't require
  // hitting the button — same as other screens treat hold as "back".
  if (currentView_ == View::RUNNING) {
    LOG_INFO(LOG_TAG, "Long-hold during calibration — cancelling");
    if (ctx_) ctx_->cancelCalibration();
  }
  if (ctx_) ctx_->back();
}

void CalibrationScreen::renderView(View v) {
  currentView_ = v;
  lastDrawnSecond_ = UINT32_MAX;
  lastDrawnSamples_ = UINT32_MAX;

  ScreenWidgets::clearContent();

  int w = M5.Display.width();
  int bw = w - 40;

  switch (v) {
    case View::READY:
      ui::applyTextStyle(ui::TextStyle::Title);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(top_center);
      M5.Display.drawString("Compass Cal", w / 2, STATUS_BAR_HEIGHT + 15);

      ui::applyTextStyle(ui::TextStyle::Body);
      M5.Display.setTextColor(TFT_LIGHTGREY);
      M5.Display.drawString("Wave through all", w / 2, STATUS_BAR_HEIGHT + 60);
      M5.Display.drawString("orientations 90 s", w / 2, STATUS_BAR_HEIGHT + 85);
      M5.Display.setTextColor(TFT_DARKGREY);
      M5.Display.drawString("in clean magnetic area", w / 2, STATUS_BAR_HEIGHT + 115);

      drawFilledButton(kSingleButtonX, kSingleButtonY, bw, kButtonHeight,
                       "Start", ButtonColor::ButtonGreen);
      break;

    case View::BLOCKED_RECORDING:
      ui::applyTextStyle(ui::TextStyle::Title);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(top_center);
      M5.Display.drawString("Compass Cal", w / 2, STATUS_BAR_HEIGHT + 15);

      ui::applyTextStyle(ui::TextStyle::Body);
      M5.Display.setTextColor(TFT_YELLOW);
      M5.Display.drawString("Stop recording", w / 2, STATUS_BAR_HEIGHT + 75);
      M5.Display.drawString("first", w / 2, STATUS_BAR_HEIGHT + 100);

      drawFilledButton(kSingleButtonX, kSingleButtonY, bw, kButtonHeight,
                       "Start", ButtonColor::ButtonGrey);
      break;

    case View::RUNNING:
      renderRunning(imu_.getCalProgress());
      break;

    case View::RESULT_OK:
    case View::RESULT_BAD:
      renderResult(imu_.getPendingResult());
      break;

    case View::UNKNOWN:
      break;
  }
}

void CalibrationScreen::renderRunning(const CalProgress& p) {
  int w = M5.Display.width();
  int bw = w - 40;
  bool firstPaint = (lastDrawnSecond_ == UINT32_MAX);

  // Static chrome + button on first paint only (renderView already cleared).
  // The panel is 320x240; everything below must stay above the button top
  // (STATUS_BAR_HEIGHT + 145 = 175) so it is actually on-screen.
  if (firstPaint) {
    ui::applyTextStyle(ui::TextStyle::Title);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString("CAL RUN", w / 2, STATUS_BAR_HEIGHT + 8);
    ui::applyTextStyle(ui::TextStyle::Body);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString("Wave all axes", w / 2, STATUS_BAR_HEIGHT + 40);
    drawFilledButton(kSingleButtonX, kSingleButtonY, bw, kButtonHeight,
                     "Cancel", ButtonColor::ButtonRed);
  }

  uint32_t remainSec = (p.elapsedMs >= 90000) ? 0 : (90000 - p.elapsedMs + 999) / 1000;

  // Repaint the countdown only when the displayed second changes. The sample
  // count ticks several times faster, and the old code repainted the big
  // size-4 digits on every tick regardless — so they visibly flashed (B7).
  if (firstPaint || remainSec != lastDrawnSecond_) {
    M5.Display.fillRect(0, STATUS_BAR_HEIGHT + 70, w, 36, TFT_BLACK);
    char buf[16];
    snprintf(buf, sizeof(buf), "%2lus", (unsigned long)remainSec);
    ui::applyTextStyle(ui::TextStyle::Large);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(buf, w / 2, STATUS_BAR_HEIGHT + 70);
  }

  // Repaint the sample-count strip only when the count changes. Was at
  // STATUS_BAR_HEIGHT + 220 (y >= 250) — off the bottom of the 240 px panel,
  // so it was never visible during calibration (B7). Now tucked above the
  // button.
  if (firstPaint || p.samples != lastDrawnSamples_) {
    M5.Display.fillRect(0, STATUS_BAR_HEIGHT + 118, w, 14, TFT_BLACK);
    ui::applyTextStyle(ui::TextStyle::Fine);
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setTextDatum(top_center);
    char nbuf[24];
    snprintf(nbuf, sizeof(nbuf), "samples %lu", (unsigned long)p.samples);
    M5.Display.drawString(nbuf, w / 2, STATUS_BAR_HEIGHT + 120);
  }

  lastDrawnSecond_ = remainSec;
  lastDrawnSamples_ = p.samples;
}

void CalibrationScreen::renderResult(const CalResult& r) {
  int w = M5.Display.width();
  int bw = w - 40;

  ui::applyTextStyle(ui::TextStyle::Title);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(top_center);
  M5.Display.drawString("Result", w / 2, STATUS_BAR_HEIGHT + 10);

  ui::applyTextStyle(ui::TextStyle::Body);
  M5.Display.setTextDatum(top_left);

  if (r.tooFewSamples) {
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("Too little motion", 20, STATUS_BAR_HEIGHT + 60);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    char buf[32];
    snprintf(buf, sizeof(buf), "samples: %lu", (unsigned long)r.sampleCount);
    M5.Display.drawString(buf, 20, STATUS_BAR_HEIGHT + 90);
    drawFilledButton(kSingleButtonX, kSingleButtonY, bw, kButtonHeight,
                     "Back", ButtonColor::ButtonGrey);
    return;
  }

  if (!r.valid) {
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("Bad fit", 20, STATUS_BAR_HEIGHT + 60);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString("Rotate more on", 20, STATUS_BAR_HEIGHT + 90);
    M5.Display.drawString("all 3 axes", 20, STATUS_BAR_HEIGHT + 115);
    drawFilledButton(kSingleButtonX, kSingleButtonY, bw, kButtonHeight,
                     "Back", ButtonColor::ButtonGrey);
    return;
  }

  // Residual band. Thresholds from imu_sketch handoff: ~0.04 clean park,
  // ~0.25 indoor-contaminated. Mid-band is amber so users err on caution.
  const char* band;
  uint16_t bandColor;
  if (r.residualStd < 0.10f) {
    band = "GOOD"; bandColor = TFT_GREEN;
  } else if (r.residualStd < 0.20f) {
    band = "FAIR"; bandColor = TFT_YELLOW;
  } else {
    band = "POOR"; bandColor = TFT_RED;
  }

  char buf[40];
  M5.Display.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof(buf), "residual: %.3f", r.residualStd);
  M5.Display.drawString(buf, 20, STATUS_BAR_HEIGHT + 50);
  M5.Display.setTextColor(bandColor);
  M5.Display.drawString(band, w - 80, STATUS_BAR_HEIGHT + 50);

  ui::applyTextStyle(ui::TextStyle::Fine);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof(buf), "samples: %lu", (unsigned long)r.sampleCount);
  M5.Display.drawString(buf, 20, STATUS_BAR_HEIGHT + 85);
  snprintf(buf, sizeof(buf), "hi: %+.1f %+.1f %+.1f",
           r.hardIron[0], r.hardIron[1], r.hardIron[2]);
  M5.Display.drawString(buf, 20, STATUS_BAR_HEIGHT + 105);
  snprintf(buf, sizeof(buf), "si: %.3f %.3f %.3f",
           r.softIron[0], r.softIron[1], r.softIron[2]);
  M5.Display.drawString(buf, 20, STATUS_BAR_HEIGHT + 120);

  drawTwoButtons("Discard", ButtonColor::ButtonGrey, "Apply", ButtonColor::ButtonGreen);
}

void CalibrationScreen::drawTwoButtons(const char* leftLabel, ButtonColor leftColor,
                                       const char* rightLabel, ButtonColor rightColor) {
  int w = M5.Display.width();
  int bw = w - 40;
  int half = (bw - 20) / 2;
  int rightX = kSingleButtonX + half + 20;
  drawFilledButton(kSingleButtonX, kSingleButtonY, half, kButtonHeight, leftLabel, leftColor);
  drawFilledButton(rightX, kSingleButtonY, half, kButtonHeight, rightLabel, rightColor);
}

