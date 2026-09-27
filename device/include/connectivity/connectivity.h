#pragma once

#include <atomic>
#include <functional>

#include "connectivity/web_server.h"
#include "hardware/gps.h"
#include "types.h"

class Connectivity {
 public:
  using StateChangeFn = std::function<void(WifiState oldState, WifiState newState)>;
  using OnConnectedFn = std::function<void()>;
  using OnRadioReleasedFn = std::function<void()>;

  static constexpr unsigned long INACTIVITY_TIMEOUT_MS = 300000;
  static constexpr unsigned long INACTIVITY_CHECK_INTERVAL_MS = 30000;

  Connectivity();

  // onRadioReleased fires after disconnect() turns the WiFi radio off, so
  // the ESP-NOW telemetry link can re-arm it (see EspnowTelemetry).
  void init(StateChangeFn onStateChange, OnConnectedFn onConnected, Storage* storage,
            OnRadioReleasedFn onRadioReleased = nullptr);

  void toggle();
  void receiveGPSData(const GPSData& data);

  WifiState getState() const { return state; }

 private:
  std::atomic<WifiState> state;
  StateChangeFn onStateChange;
  OnConnectedFn onConnected;
  OnRadioReleasedFn onRadioReleased;
  WebServer webServer;
  TaskHandle_t inactivityTaskHandle = nullptr;

  void transitionState(WifiState newState);
  bool connectWifi();
  void disconnect();

  static void connectTask(void* pvParameters);
  static void inactivityTask(void* pvParameters);
};
