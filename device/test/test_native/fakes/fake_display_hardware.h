#pragma once

#include "ui/ports/display_hardware.h"

class FakeDisplayHardware : public DisplayHardware {
 public:
  void lockSPI() override { lockCount++; }
  void unlockSPI() override { unlockCount++; }

  void displaySleep() override { sleeping = true; sleepCallCount++; }
  void displayWakeup() override { sleeping = false; wakeCallCount++; }
  void setBrightness(uint8_t level) override { brightness = level; }

  int getBatteryLevel() override { return batteryLevel; }

  void fillScreen(uint32_t color) override { lastFillColor = color; }
  void fillRect(int x, int y, int w, int h, uint32_t color) override { (void)x; (void)y; (void)w; (void)h; (void)color; }
  void setTextStyle(ui::TextStyle style) override { lastTextStyle = style; }
  void setTextColor(uint32_t color) override { (void)color; }
  void setTextDatum(uint8_t datum) override { (void)datum; }
  void drawString(const char* text, int x, int y) override { (void)text; (void)x; (void)y; }
  void setCursor(int x, int y) override { (void)x; (void)y; }
  void println(const char* text) override { lastPrintln = text; }

  int width() override { return 320; }
  int height() override { return 240; }

  // Test inspection
  int lockCount = 0;
  int unlockCount = 0;
  bool sleeping = false;
  int sleepCallCount = 0;
  int wakeCallCount = 0;
  uint8_t brightness = 0;
  int batteryLevel = 100;
  uint32_t lastFillColor = 0;
  const char* lastPrintln = nullptr;
  ui::TextStyle lastTextStyle = ui::TextStyle::Body;
};
