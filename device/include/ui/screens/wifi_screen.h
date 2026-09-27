#pragma once

#include "ui/screens/screen.h"
#include "ui/screens/screen_shared.h"
#include "types.h"

class WifiScreen : public Screen {
 public:
  WifiScreen();
  void init(ScreenContext* ctx) override;
  void render() override;
  void onEnter() override;
  void handleClick(int x, int y) override;
  void handleHold(int x, int y) override;

  void onWifiStateChanged(WifiState state) override;

 private:
  void renderFull(WifiState wifiState);
  void drawStatusAndButton(WifiState wifiState);
  void drawWifiButton(const char* label, ButtonColor color);

  ScreenContext* ctx_ = nullptr;
  WifiState currentWifiState = WifiState::DISCONNECTED;
  int buttonWidth;
  int buttonHeight;
};
