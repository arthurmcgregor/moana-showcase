#include "ui/screens/wifi_screen.h"

#include <M5Unified.h>

#include "ui/screen_context.h"
#include "ui/screens/touch_targets.h"
#include "ui/screens/screen_shared.h"
#include "ui/screens/screen_widgets.h"
#include "ui/text_style.h"
#include "utils/logger.h"

static const char* LOG_TAG = "WifiScreen";

namespace {

struct WifiVisual {
  const char* status;
  uint16_t statusColor;
  const char* button;
  ButtonColor buttonColor;
};

// One source of truth for how each WiFi state looks, so the status line and the
// button never disagree. CONNECTING greys the button (no re-tap mid-connect)
// and shows "Connecting..." — mirrors how SyncScreen greys to "Syncing..."
// (R8 / QW12).
WifiVisual visualFor(WifiState s) {
  switch (s) {
    case WifiState::CONNECTED:
      return {"Connected", TFT_GREEN, "Disconnect", ButtonColor::ButtonRed};
    case WifiState::CONNECTING:
      return {"Connecting...", TFT_YELLOW, "Connecting...", ButtonColor::ButtonGrey};
    case WifiState::ERROR:
      return {"Connection failed", TFT_RED, "Connect", ButtonColor::ButtonGreen};
    case WifiState::DISCONNECTED:
    default:
      return {"Not connected", TFT_LIGHTGREY, "Connect", ButtonColor::ButtonGreen};
  }
}

}  // namespace

WifiScreen::WifiScreen() {}

void WifiScreen::init(ScreenContext* ctx) {
  LOG_INFO(LOG_TAG, "WifiScreen::init called");
  ctx_ = ctx;
  buttonWidth = M5.Display.width() - 40;  // 20px padding on each side
  buttonHeight = 80;
}

void WifiScreen::render() {
  renderFull(currentWifiState);
}

void WifiScreen::onEnter() {
  // Connecting is the overwhelmingly common reason to open this screen, so kick
  // off a connect on entry and save the extra tap (#135). Reuses the exact path
  // a tap takes: toggleWifi() begins a connect from DISCONNECTED/ERROR (and is a
  // no-op mid-connect). The CONNECTING repaint then arrives async via
  // onWifiStateChanged(). CONNECTED is excluded so we never drop a live link.
  // onEnter() only fires on a real transition, so this won't re-fire on wake.
  if (currentWifiState == WifiState::DISCONNECTED || currentWifiState == WifiState::ERROR) {
    LOG_INFO(LOG_TAG, "Wifi screen opened while disconnected - auto-connecting");
    ctx_->toggleWifi();
  }
}

void WifiScreen::onWifiStateChanged(WifiState state) {
  if (state == currentWifiState) {
    return;
  }
  currentWifiState = state;

  // Display broadcasts this hook to every screen (B12); only paint when we're
  // the visible screen. When inactive we just keep the cache current so
  // render() shows the right thing on re-entry.
  if (ctx_->current() != ScreenState::WIFI_SCREEN) {
    return;
  }

  // Repaint the status line + button together (leaves the title untouched, so
  // no flash on each transition).
  drawStatusAndButton(state);
}

void WifiScreen::renderFull(WifiState wifiState) {
  currentWifiState = wifiState;

  ScreenWidgets::clearContent();

  // Draw title
  ui::applyTextStyle(ui::TextStyle::Title);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(top_center);
  M5.Display.drawString("Wifi", M5.Display.width() / 2, STATUS_BAR_HEIGHT + 20);

  drawStatusAndButton(wifiState);
}

void WifiScreen::drawStatusAndButton(WifiState wifiState) {
  WifiVisual v = visualFor(wifiState);
  int w = M5.Display.width();

  // Clear + redraw the status line (the button repaints fully itself).
  M5.Display.fillRect(0, STATUS_BAR_HEIGHT + 60, w, 30, TFT_BLACK);
  ui::applyTextStyle(ui::TextStyle::Body);
  M5.Display.setTextColor(v.statusColor);
  M5.Display.setTextDatum(top_center);
  M5.Display.drawString(v.status, w / 2, STATUS_BAR_HEIGHT + 70);

  drawWifiButton(v.button, v.buttonColor);
}

void WifiScreen::handleClick(int x, int y) {
  // Button is disabled mid-connect (greyed "Connecting..."); ignore taps until
  // the attempt resolves.
  if (currentWifiState == WifiState::CONNECTING) {
    return;
  }

  int buttonX = 20;
  int buttonY = STATUS_BAR_HEIGHT + 120;

  if (TouchTargets::isInsideButton(x, y, buttonX, buttonY, buttonWidth, buttonHeight)) {
    LOG_DEBUG(LOG_TAG, "Wifi button pressed");
    ctx_->toggleWifi();
  }
}

void WifiScreen::handleHold(int x, int y) {
  LOG_INFO(LOG_TAG, "Wifi screen held - return to menu");
  ctx_->back();
}


void WifiScreen::drawWifiButton(const char* label, ButtonColor color) {
  ScreenWidgets::drawFilledButton(20, STATUS_BAR_HEIGHT + 120, buttonWidth, buttonHeight, label,
                                  color);
}
