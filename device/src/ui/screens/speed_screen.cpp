#include "ui/screens/speed_screen.h"

#include <M5Unified.h>

#include "ui/screen_context.h"
#include "ui/screens/screen_shared.h"
#include "ui/screens/screen_widgets.h"
#include "ui/text_style.h"
#include "utils/logger.h"

static const char* LOG_TAG = "SpeedScreen";

SpeedScreen::SpeedScreen() : current_speed(0.0) {}

void SpeedScreen::init(ScreenContext* ctx) {
  ctx_ = ctx;
}

void SpeedScreen::render() {
  ScreenWidgets::clearContent();

  gpsState_ = ctx_ ? ctx_->gpsState() : GPSState::LOCKED;
  drawSpeed(current_speed);
}

void SpeedScreen::updateGPSData(const GPSData& data) {
  // A fresh fix means the data is live again — clear any prior stale styling.
  gpsState_ = GPSState::LOCKED;

  // Only update display if speed changes significantly (0.1 knot)
  if (fabs(data.speed - current_speed) < 0.09) {
    return;
  }

  drawSpeed(data.speed);
}

void SpeedScreen::onStatusChanged(GPSState gps, RecordingState rec) {
  (void)rec;
  if (gps == gpsState_) {
    return;
  }
  gpsState_ = gps;
  // Only the active screen is notified, and the SPI lock is already held by
  // the caller (Display::updateStatusBar), so repaint the number in place.
  if (ctx_ && ctx_->current() == ScreenState::SPEED_SCREEN) {
    ScreenWidgets::clearContent();
    drawSpeed(current_speed);
  }
}

void SpeedScreen::drawSpeed(float speed) {
  int contentHeight = M5.Display.height() - STATUS_BAR_HEIGHT;
  int x = M5.Display.width() / 2;
  int y = STATUS_BAR_HEIGHT + contentHeight / 2;

  const bool stale = (gpsState_ == GPSState::LOST);

  // Mutex is held by caller
  ui::applyTextStyle(ui::TextStyle::Speed);
  M5.Display.setTextColor(stale ? COLOR_STALE : COLOR_LIVE, BLACK);
  M5.Display.setTextDatum(middle_center);
  if (speed < 10.0) {
    M5.Display.drawString(String(speed, 1), x, y);
  } else {
    M5.Display.drawString(String(speed, 0), x, y);
  }

  if (stale) {
    ui::applyTextStyle(ui::TextStyle::Title);
    M5.Display.setTextColor(COLOR_WARN, BLACK);
    M5.Display.drawString("NO FIX", x, STATUS_BAR_HEIGHT + contentHeight - 30);
  }

  current_speed = speed;
}

void SpeedScreen::handleClick(int x, int y) {
  LOG_INFO(LOG_TAG, "Speed screen tapped - change to detail view");
  ctx_->show(ScreenState::DETAIL_SCREEN);
}

void SpeedScreen::handleHold(int x, int y) {
  LOG_INFO(LOG_TAG, "Speed screen held - return to menu");
  ctx_->back();
}
