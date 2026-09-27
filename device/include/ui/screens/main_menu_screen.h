#pragma once

#include "ui/screens/screen.h"
#include "ui/screens/screen_shared.h"
#include "types.h"

class ScreenContext;

class MainMenuScreen : public Screen {
 public:
  MainMenuScreen();
  void init(ScreenContext* ctx) override;
  void render() override;
  void handleClick(int x, int y) override;

  void onStatusChanged(GPSState gps, RecordingState rec) override;

 private:
  void renderPage();
  void drawSlot(int slot, const char* label, ButtonColor color);
  void drawButton(int x, int y, const char* label, ButtonColor color);

  ScreenContext* ctx_ = nullptr;
  bool current_gps_enabled = false;
  bool current_recording_enabled = false;
  int page_ = 0;

  int contentHeight;
  int buttonWidth;
  int buttonHeight;
};
