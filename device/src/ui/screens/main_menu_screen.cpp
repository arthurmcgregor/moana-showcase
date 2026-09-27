#include "ui/screens/main_menu_screen.h"

#include <M5Unified.h>

#include "ui/screen_context.h"
#include "ui/screens/menu_pagination.h"
#include "ui/screens/touch_targets.h"
#include "ui/screens/screen_shared.h"
#include "ui/screens/screen_widgets.h"
#include "utils/logger.h"

static const char* LOG_TAG = "MainMenuScreen";

// Menu definition — append new items here. Pagination is automatic: if all
// items fit on one page (<= 4 items) no pager is shown; otherwise every page
// reserves the bottom-right slot for ">>" (3 items per page), wrapping from
// the last page back to page 0. Order items most-used first.

namespace {

struct MenuItem {
  // `dynamic` flags items whose label/color depends on status — only these
  // are repainted on status changes (avoids redrawing static buttons).
  const char* (*label)(bool gpsEnabled, bool recordingEnabled);
  ButtonColor (*color)(bool gpsEnabled, bool recordingEnabled);
  void (*onTap)(ScreenContext*);
  bool dynamic;
};

const char* speedLabel(bool, bool) { return "Speed"; }
ButtonColor speedColor(bool gps, bool) {
  return gps ? ButtonColor::ButtonGreen : ButtonColor::ButtonGrey;
}

const char* syncLabel(bool, bool) { return "Sync"; }

const char* recordLabel(bool, bool rec) { return rec ? "Stop" : "Start"; }
ButtonColor recordColor(bool gps, bool rec) {
  if (!gps) return ButtonColor::ButtonGrey;
  return rec ? ButtonColor::ButtonRed : ButtonColor::ButtonGreen;
}

const char* wifiLabel(bool, bool) { return "Wifi"; }

const char* calibrateLabel(bool, bool) { return "Calibrate"; }

ButtonColor staticGreen(bool, bool) { return ButtonColor::ButtonGreen; }

const MenuItem kItems[] = {
    // Page 0 — most-used. Sync sits next to Record because they pair on a sail day.
    {speedLabel,     speedColor,  [](ScreenContext* c) { c->show(ScreenState::SPEED_SCREEN); },       true},
    {syncLabel,      staticGreen, [](ScreenContext* c) { c->show(ScreenState::SYNC_SCREEN); },        false},
    {recordLabel,    recordColor, [](ScreenContext* c) { c->toggleRecording(); },                     true},
    // Page 1 — infrequent admin tasks.
    {wifiLabel,      staticGreen, [](ScreenContext* c) { c->show(ScreenState::WIFI_SCREEN); },        false},
    {calibrateLabel, staticGreen, [](ScreenContext* c) { c->show(ScreenState::CALIBRATION_SCREEN); }, false},
    // Add more items here.
};
constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

}  // namespace

MainMenuScreen::MainMenuScreen() {}

void MainMenuScreen::init(ScreenContext* ctx) {
  LOG_INFO(LOG_TAG, "MainMenuScreen::init called");
  ctx_ = ctx;
  contentHeight = M5.Display.height() - STATUS_BAR_HEIGHT;
  buttonWidth = M5.Display.width() / 2;
  buttonHeight = contentHeight / 2;
}

void MainMenuScreen::handleClick(int x, int y) {
  int slot = TouchTargets::mainMenuSlot(x, y, M5.Display.width(), M5.Display.height());
  LOG_DEBUG(LOG_TAG, "Button pressed at x=%d, y=%d (slot=%d, page=%d)", x, y, slot, page_);

  // Status-bar tap (slot < 0): ignore. Guarding here as well as in mainMenuSlot
  // because itemIndexForSlot() can map a negative slot to a real item on pages
  // past the first (page * 3 + slot).
  if (slot < 0) return;

  if (MenuPagination::slotIsNextButton(slot, kItemCount)) {
    page_ = (page_ + 1) % MenuPagination::pageCount(kItemCount);
    renderPage();
    return;
  }
  int idx = MenuPagination::itemIndexForSlot(slot, page_, kItemCount);
  if (idx >= 0) kItems[idx].onTap(ctx_);
}

void MainMenuScreen::render() {
  // Always land on the first page when the menu is entered (including after a
  // long-press back from a sub-screen). Most-used items live there.
  page_ = 0;
  renderPage();
}

void MainMenuScreen::onStatusChanged(GPSState gps, RecordingState rec) {
  bool newGps = (gps == GPSState::LOCKED);
  bool newRec = (rec == RecordingState::ACTIVE);
  if (newGps == current_gps_enabled && newRec == current_recording_enabled) return;

  current_gps_enabled = newGps;
  current_recording_enabled = newRec;

  for (int slot = 0; slot < MenuPagination::kSlotsPerPage; ++slot) {
    if (MenuPagination::slotIsNextButton(slot, kItemCount)) continue;
    int idx = MenuPagination::itemIndexForSlot(slot, page_, kItemCount);
    if (idx < 0) continue;
    if (!kItems[idx].dynamic) continue;
    drawSlot(slot, kItems[idx].label(current_gps_enabled, current_recording_enabled),
             kItems[idx].color(current_gps_enabled, current_recording_enabled));
  }
}

void MainMenuScreen::renderPage() {
  ScreenWidgets::clearContent();

  for (int slot = 0; slot < MenuPagination::kSlotsPerPage; ++slot) {
    if (MenuPagination::slotIsNextButton(slot, kItemCount)) {
      drawSlot(slot, ">>", ButtonColor::ButtonGreen);
      continue;
    }
    int idx = MenuPagination::itemIndexForSlot(slot, page_, kItemCount);
    if (idx < 0) continue;
    const auto& item = kItems[idx];
    drawSlot(slot, item.label(current_gps_enabled, current_recording_enabled),
             item.color(current_gps_enabled, current_recording_enabled));
  }
}

void MainMenuScreen::drawSlot(int slot, const char* label, ButtonColor color) {
  int x = (slot % 2) * buttonWidth;
  int y = STATUS_BAR_HEIGHT + (slot / 2) * buttonHeight;
  drawButton(x, y, label, color);
}

void MainMenuScreen::drawButton(int x, int y, const char* label, ButtonColor color) {
  ScreenWidgets::drawFilledButton(x, y, buttonWidth, buttonHeight, label, color);
}
