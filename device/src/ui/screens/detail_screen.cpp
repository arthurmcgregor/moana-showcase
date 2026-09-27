#include "ui/screens/detail_screen.h"

#include <M5Unified.h>

#include "ui/screen_context.h"
#include "ui/screens/screen_shared.h"
#include "ui/text_style.h"
#include "ui/screens/screen_widgets.h"
#include "utils/logger.h"

static const char* LOG_TAG = "DetailScreen";

DetailScreen::DetailScreen() : current_speed(0.0) {}

void DetailScreen::init(ScreenContext* ctx) {
  ctx_ = ctx;
}

void DetailScreen::render() {
  ScreenWidgets::clearContent();
  gpsState_ = ctx_ ? ctx_->gpsState() : GPSState::LOCKED;
  // No fix arrives while LOST, so entering during LOST must repaint the cached
  // fix immediately — otherwise the screen would sit blank. When LOCKED, wait
  // for the next GPS update (arrives ~1 Hz).
  if (gpsState_ == GPSState::LOST && haveData_) {
    drawDetails(lastData_);
  }
}

void DetailScreen::updateGPSData(const GPSData& data) {
  gpsState_ = GPSState::LOCKED;  // fresh fix — data is live again
  lastData_ = data;
  haveData_ = true;
  drawDetails(data);
}

void DetailScreen::onStatusChanged(GPSState gps, RecordingState rec) {
  (void)rec;
  if (gps == gpsState_) {
    return;
  }
  gpsState_ = gps;
  // Active screen only; SPI lock already held by Display::updateStatusBar.
  if (ctx_ && ctx_->current() == ScreenState::DETAIL_SCREEN && haveData_) {
    ScreenWidgets::clearContent();
    drawDetails(lastData_);
  }
}

void DetailScreen::drawDetails(const GPSData& data) {
  const bool stale = (gpsState_ == GPSState::LOST);

  ui::applyTextStyle(ui::TextStyle::Title);
  M5.Display.setTextColor(stale ? COLOR_STALE : COLOR_LIVE, BLACK);
  M5.Display.setTextDatum(top_left);
  M5.Display.setCursor(0, STATUS_BAR_HEIGHT + 10);

  // snprintf into a fixed buffer rather than String concatenation — this runs
  // on every 1 Hz GPS update, and the old "..." + String(...) form heap-
  // allocated several temporaries per line (QW11). The rest of the hot path is
  // already zero-alloc.
  char line[32];
  snprintf(line, sizeof(line), "Spd: %.1f kt", data.speed);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "Lat: %.6f", data.lat);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "Lng: %.6f", data.lng);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "Crs: %.2f", data.course);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "Alt: %.2f m", data.altitude);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "Acc: %.2f", data.hdop);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "Sat: %d", data.satellites);
  M5.Display.println(line);

  if (stale) {
    M5.Display.setTextColor(COLOR_WARN, BLACK);
    M5.Display.println("NO FIX");
  }
}

void DetailScreen::handleClick(int x, int y) {
  LOG_INFO(LOG_TAG, "Detail screen tapped - change to speed view");
  ctx_->show(ScreenState::SPEED_SCREEN);
}

void DetailScreen::handleHold(int x, int y) {
  LOG_INFO(LOG_TAG, "Detail screen held - return to menu");
  ctx_->back();
}
