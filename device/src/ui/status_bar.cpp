#include "ui/status_bar.h"

#include <M5Unified.h>

#include "lgfx/v1/misc/enum.hpp"
#include "ui/screens/screen_shared.h"
#include "ui/text_style.h"
#include "utility/rtc/RTC_Base.hpp"
#include "utils/logger.h"

static const char* LOG_TAG = "StatusBar";

StatusBar::StatusBar()
    : current_gps_display(GPSDisplay::Acquiring),
      current_wifi_display(WifiDisplay::Disconnected),
      current_battery_percentage(std::nullopt),
      current_time(std::nullopt) {}

void StatusBar::render() {
  LOG_DEBUG(LOG_TAG, "Rendering status bar");
  // Clear status bar area
  M5.Display.fillRect(0, 0, M5.Display.width(), STATUS_BAR_HEIGHT, BLACK);

  prepareDisplay();
  drawLabels();
  drawGPSDisplay(current_gps_display);
  drawWifiDisplay(current_wifi_display);
  drawBatteryDisplay(current_battery_percentage);
  drawTimeDisplay(current_time);
}

void StatusBar::updateGPS(GPSDisplay gps_display) {
  if (current_gps_display != gps_display) {
    LOG_DEBUG(LOG_TAG, "GPS display updated: %s", gpsDisplayToString(gps_display));
    prepareDisplay();
    drawGPSDisplay(gps_display);
    current_gps_display = gps_display;
  }
}

void StatusBar::updateWifi(WifiDisplay wifi_display) {
  if (current_wifi_display != wifi_display) {
    LOG_DEBUG(LOG_TAG, "Wifi display updated: %s", wifiDisplayToString(wifi_display));
    prepareDisplay();
    drawWifiDisplay(wifi_display);
    current_wifi_display = wifi_display;
  }
}

void StatusBar::updateBattery(BatteryPercentage battery_percentage) {
  if (current_battery_percentage != battery_percentage) {
    int value = battery_percentage ? *battery_percentage : -1;
    LOG_DEBUG(LOG_TAG, "Battery display updated: %d%%", value);
    prepareDisplay();
    drawBatteryDisplay(battery_percentage);
    current_battery_percentage = battery_percentage;
  }
}

void StatusBar::updateTime(const m5::rtc_time_t& time) {
  if (!current_time || current_time->hours != time.hours || current_time->minutes != time.minutes) {
    LOG_DEBUG(LOG_TAG, "Time display updated: %02d:%02d:%02d", time.hours, time.minutes,
              time.seconds);
    current_time = time;
    prepareDisplay();
    drawTimeDisplay(current_time);
  }
}

void StatusBar::prepareDisplay() {
  ui::applyTextStyle(ui::TextStyle::Body);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextDatum(top_left);
}

void StatusBar::drawLabels() {
  M5.Display.setCursor(GPS_X, LABEL_Y);
  M5.Display.print("GPS:");

  M5.Display.setCursor(WIFI_X, LABEL_Y);
  M5.Display.print("Wifi:");
}

void StatusBar::drawGPSDisplay(GPSDisplay gps_display) {
  int dotX = GPS_X + 60;
  int dotY = LABEL_Y + 6;

  switch (gps_display) {
    case GPSDisplay::Acquiring:  // Yellow
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_YELLOW);
      break;
    case GPSDisplay::Idle:  // Green outline
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_BLACK);
      M5.Display.drawCircle(dotX, dotY, DOT_RADIUS, TFT_GREEN);
      M5.Display.drawCircle(dotX, dotY, DOT_RADIUS - 1, TFT_GREEN);
      break;
    case GPSDisplay::Recording:  // Green
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_GREEN);
      break;
    case GPSDisplay::Busy:  // Yellow outline
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_BLACK);
      M5.Display.drawCircle(dotX, dotY, DOT_RADIUS, TFT_YELLOW);
      M5.Display.drawCircle(dotX, dotY, DOT_RADIUS - 1, TFT_YELLOW);
      break;
    case GPSDisplay::Error:  // Red
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_RED);
      break;
  }
}

void StatusBar::drawWifiDisplay(WifiDisplay wifi_display) {
  int dotX = WIFI_X + 70;
  int dotY = LABEL_Y + 6;

  switch (wifi_display) {
    case WifiDisplay::Disconnected:  // Yellow
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_BLACK);
      M5.Display.drawCircle(dotX, dotY, DOT_RADIUS, 0xFFE0);
      M5.Display.drawCircle(dotX, dotY, DOT_RADIUS - 1, 0xFFE0);
      break;
    case WifiDisplay::Connected:  // Green
      M5.Display.fillCircle(dotX, dotY, DOT_RADIUS, TFT_GREEN);
      break;
  }
}

void StatusBar::drawBatteryDisplay(BatteryPercentage battery_percentage) {
  M5.Display.setCursor(M5.Display.width() - BATTERY_X_OFFSET, LABEL_Y);
  if (battery_percentage) {
    M5.Display.printf("%3d%%", *battery_percentage);
  } else {
    M5.Display.print("    ");  // 4 spaces, matches width of "100%"
  }
}

void StatusBar::drawTimeDisplay(const std::optional<m5::rtc_time_t>& time) {
  M5.Display.setCursor(TIME_X, LABEL_Y);
  if (time) {
    char timeStr[16];  // sized for GCC's worst-case %d estimate, not just "HH:MM"
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", time->hours, time->minutes);
    M5.Display.print(timeStr);
  }
}

const char* StatusBar::gpsDisplayToString(GPSDisplay gps_display) {
  switch (gps_display) {
    case GPSDisplay::Acquiring:
      return "Acquiring";
    case GPSDisplay::Idle:
      return "Idle";
    case GPSDisplay::Recording:
      return "Recording";
    case GPSDisplay::Busy:
      return "Busy";
    case GPSDisplay::Error:
      return "Error";
    default:
      return "Unknown";
  }
}

const char* StatusBar::wifiDisplayToString(WifiDisplay wifi_display) {
  switch (wifi_display) {
    case WifiDisplay::Disconnected:
      return "Disconnected";
    case WifiDisplay::Connected:
      return "Connected";
    default:
      return "Unknown";
  }
}
