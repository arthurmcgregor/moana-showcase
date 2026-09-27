#pragma once

#include <cstdint>
#include <optional>

// Stub m5::rtc_time_t for native tests
namespace m5 {
struct rtc_time_t {
  uint8_t hours = 0;
  uint8_t minutes = 0;
  uint8_t seconds = 0;
};
}  // namespace m5

enum class GPSDisplay { Acquiring, Idle, Recording, Busy, Error };
enum class WifiDisplay { Disconnected, Connected };
using BatteryPercentage = std::optional<int>;

class StatusBar {
 public:
  StatusBar() {}
  void render() {}
  void updateGPS(GPSDisplay gps_display) { (void)gps_display; }
  void updateWifi(WifiDisplay wifi_display) { (void)wifi_display; }
  void updateBattery(BatteryPercentage battery_percentage) { (void)battery_percentage; }
  void updateTime(const m5::rtc_time_t& time) { (void)time; }
};
