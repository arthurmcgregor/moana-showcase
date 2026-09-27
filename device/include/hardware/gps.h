#pragma once

#include <TinyGPSPlus.h>

#include "hardware/gps_types.h"

class App;

class GPS {
 public:
  // A LOCKED fix is considered lost once its last update is older than this.
  static constexpr unsigned long FIX_STALE_MS = 10000;

  GPS();
  void init(App* app);
  bool getLatestData(GPSData& data);
  // Milliseconds since the last valid location update (TinyGPSPlus age).
  unsigned long fixAgeMs();
  static void dataTask(void* pvParameters);
  static void healthTask(void* pvParameters);

 private:
  TinyGPSPlus gps;
  HardwareSerial serial;
  int uart_num;
  int rx_pin;
  int tx_pin;
  uint32_t baud;

  void read();
};
