#pragma once

#include <cstdint>

#include "ui/text_style.h"

// Abstracts the physical display hardware (M5.Display, SPI bus, power).
// In production, implemented by M5DisplayHardware.
// In tests, implemented by FakeDisplayHardware.
class DisplayHardware {
 public:
  virtual ~DisplayHardware() = default;

  // SPI mutex
  virtual void lockSPI() = 0;
  virtual void unlockSPI() = 0;

  // Display power
  virtual void displaySleep() = 0;
  virtual void displayWakeup() = 0;
  virtual void setBrightness(uint8_t level) = 0;

  // Battery
  virtual int getBatteryLevel() = 0;

  // Drawing primitives (used by full-screen messages only)
  virtual void fillScreen(uint32_t color) = 0;
  virtual void fillRect(int x, int y, int w, int h, uint32_t color) = 0;
  virtual void setTextStyle(ui::TextStyle style) = 0;
  virtual void setTextColor(uint32_t color) = 0;
  virtual void setTextDatum(uint8_t datum) = 0;
  virtual void drawString(const char* text, int x, int y) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void println(const char* text) = 0;

  // Screen dimensions
  virtual int width() = 0;
  virtual int height() = 0;
};
