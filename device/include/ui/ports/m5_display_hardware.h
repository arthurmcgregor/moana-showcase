#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ui/ports/display_hardware.h"

// Production DisplayHardware backed by M5Unified and a FreeRTOS SPI mutex.
class M5DisplayHardware : public DisplayHardware {
 public:
  M5DisplayHardware();

  void lockSPI() override;
  void unlockSPI() override;

  void displaySleep() override;
  void displayWakeup() override;
  void setBrightness(uint8_t level) override;

  int getBatteryLevel() override;

  void fillScreen(uint32_t color) override;
  void fillRect(int x, int y, int w, int h, uint32_t color) override;
  void setTextStyle(ui::TextStyle style) override;
  void setTextColor(uint32_t color) override;
  void setTextDatum(uint8_t datum) override;
  void drawString(const char* text, int x, int y) override;
  void setCursor(int x, int y) override;
  void println(const char* text) override;

  int width() override;
  int height() override;

 private:
  SemaphoreHandle_t spiMutex_;
};
