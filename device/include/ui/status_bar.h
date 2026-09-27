#pragma once

#include <optional>

#include "utility/rtc/RTC_Base.hpp"

enum class GPSDisplay { Acquiring, Idle, Recording, Busy, Error };
enum class WifiDisplay { Disconnected, Connected };
using BatteryPercentage = std::optional<int>;

class StatusBar {
 public:
  StatusBar();
  void render();
  void updateGPS(GPSDisplay gps_display);
  void updateWifi(WifiDisplay wifi_display);
  void updateBattery(BatteryPercentage battery_percentage);
  void updateTime(const m5::rtc_time_t& time);

 private:
  static constexpr int DOT_RADIUS = 8;
  static constexpr int LABEL_Y = 12;
  static constexpr int GPS_X = 5;
  static constexpr int WIFI_X = 95;
  static constexpr int BATTERY_X_OFFSET = 60;  // Offset from right edge
  static constexpr int TIME_X = 188;           // X position for time display

  GPSDisplay current_gps_display;
  WifiDisplay current_wifi_display;
  BatteryPercentage current_battery_percentage;
  std::optional<m5::rtc_time_t> current_time;

  void prepareDisplay();
  void drawLabels();
  void drawGPSDisplay(GPSDisplay gps_display);
  void drawWifiDisplay(WifiDisplay wifi_display);
  void drawBatteryDisplay(BatteryPercentage battery_percentage);
  void drawTimeDisplay(const std::optional<m5::rtc_time_t>& time);
  const char* gpsDisplayToString(GPSDisplay gps_display);
  const char* wifiDisplayToString(WifiDisplay wifi_display);
};
