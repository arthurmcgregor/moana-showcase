#include "ui/ports/m5_display_hardware.h"

#include <M5Unified.h>

#include "utils/logger.h"

static const char* LOG_TAG = "M5DisplayHW";

M5DisplayHardware::M5DisplayHardware() {
  // Recursive: a single task legitimately re-enters the lock — e.g. the touch
  // task holds it around handleClick() (B5), and a tap that navigates calls
  // showScreen()/wake()/sleep(), each of which locks again. Cross-task callers
  // (app-controller, sync, GPS-display) still mutually exclude.
  spiMutex_ = xSemaphoreCreateRecursiveMutex();
}

void M5DisplayHardware::lockSPI() {
  if (xSemaphoreTakeRecursive(spiMutex_, portMAX_DELAY) != pdTRUE) {
    LOG_ERROR(LOG_TAG, "Failed to acquire SPI mutex");
  }
}

void M5DisplayHardware::unlockSPI() {
  xSemaphoreGiveRecursive(spiMutex_);
}

void M5DisplayHardware::displaySleep() {
  M5.Display.sleep();
}

void M5DisplayHardware::displayWakeup() {
  M5.Display.wakeup();
}

void M5DisplayHardware::setBrightness(uint8_t level) {
  M5.Display.setBrightness(level);
}

int M5DisplayHardware::getBatteryLevel() {
  return M5.Power.getBatteryLevel();
}

void M5DisplayHardware::fillScreen(uint32_t color) {
  M5.Display.fillScreen(color);
}

void M5DisplayHardware::fillRect(int x, int y, int w, int h, uint32_t color) {
  M5.Display.fillRect(x, y, w, h, color);
}

void M5DisplayHardware::setTextStyle(ui::TextStyle style) {
  ui::applyTextStyle(style);
}

void M5DisplayHardware::setTextColor(uint32_t color) {
  M5.Display.setTextColor(color);
}

void M5DisplayHardware::setTextDatum(uint8_t datum) {
  M5.Display.setTextDatum(datum);
}

void M5DisplayHardware::drawString(const char* text, int x, int y) {
  M5.Display.drawString(text, x, y);
}

void M5DisplayHardware::setCursor(int x, int y) {
  M5.Display.setCursor(x, y);
}

void M5DisplayHardware::println(const char* text) {
  M5.Display.println(text);
}

int M5DisplayHardware::width() {
  return M5.Display.width();
}

int M5DisplayHardware::height() {
  return M5.Display.height();
}
