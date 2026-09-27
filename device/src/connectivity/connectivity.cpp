#include "connectivity/connectivity.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "connectivity/connectivity_policy.h"
#include "connectivity/wifi_secrets.h"
#include "utils/logger.h"
#include "utils/state_strings.h"

static const char* LOG_TAG = "Connectivity";
static constexpr const char* MDNS_HOSTNAME = "moana";

Connectivity::Connectivity() : state(WifiState::DISCONNECTED) {}

void Connectivity::init(StateChangeFn onStateChange, OnConnectedFn onConnected, Storage* storage,
                        OnRadioReleasedFn onRadioReleased) {
  this->onStateChange = onStateChange;
  this->onConnected = onConnected;
  this->onRadioReleased = onRadioReleased;
  webServer.init(storage);
}

void Connectivity::transitionState(WifiState newState) {
  if (state == newState) {
    return;
  }

  LOG_INFO(LOG_TAG, "Wifi transition: %s -> %s", wifiStateToString(state),
           wifiStateToString(newState));

  WifiState oldState = state;
  state = newState;

  if (onStateChange) {
    onStateChange(oldState, newState);
  }
}

void Connectivity::toggle() {
  switch (state) {
    case WifiState::DISCONNECTED:
    case WifiState::ERROR:
      transitionState(WifiState::CONNECTING);
      xTaskCreate(Connectivity::connectTask, "WifiConnect", 8192, this, 2, nullptr);
      break;

    case WifiState::CONNECTED:
      disconnect();
      break;

    case WifiState::CONNECTING:
      LOG_DEBUG(LOG_TAG, "Wifi transition in progress, ignoring toggle request");
      break;
  }
}

void Connectivity::connectTask(void* pvParameters) {
  Connectivity* conn = static_cast<Connectivity*>(pvParameters);

  if (conn->connectWifi()) {
    LOG_INFO(LOG_TAG, "WiFi connected successfully");

    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err == ESP_OK) {
      LOG_INFO(LOG_TAG, "WiFi power save mode enabled (MIN_MODEM)");
    } else {
      LOG_WARN(LOG_TAG, "Failed to enable WiFi power save: %d", err);
    }

    conn->transitionState(WifiState::CONNECTED);
    conn->webServer.begin();

    // 4096: the timeout path runs disconnect() — MDNS/WiFi teardown plus the
    // state-change callback chain into Display — which overflows 2048 (R4;
    // stack canary observed on device).
    xTaskCreate(Connectivity::inactivityTask, "WifiInactivity", 4096, conn, 1,
                &conn->inactivityTaskHandle);

    if (conn->onConnected) {
      conn->onConnected();
    }
  } else {
    LOG_WARN(LOG_TAG, "WiFi connection failed");
    conn->transitionState(WifiState::DISCONNECTED);
  }

  vTaskDelete(nullptr);
}

void Connectivity::disconnect() {
  WifiState expected = WifiState::CONNECTED;
  if (!state.compare_exchange_strong(expected, WifiState::DISCONNECTED)) {
    return;
  }

  if (inactivityTaskHandle != nullptr) {
    TaskHandle_t task = inactivityTaskHandle;
    inactivityTaskHandle = nullptr;
    // Don't self-delete — let the task exit its loop naturally
    if (xTaskGetCurrentTaskHandle() != task) {
      vTaskDelete(task);
    }
  }

  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LOG_INFO(LOG_TAG, "WiFi disconnected and radio off");

  if (onStateChange) {
    onStateChange(WifiState::CONNECTED, WifiState::DISCONNECTED);
  }

  // Hand the radio back to the ESP-NOW telemetry link.
  if (onRadioReleased) {
    onRadioReleased();
  }
}

void Connectivity::inactivityTask(void* pvParameters) {
  Connectivity* conn = static_cast<Connectivity*>(pvParameters);

  while (conn->state == WifiState::CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(INACTIVITY_CHECK_INTERVAL_MS));

    if (conn->state != WifiState::CONNECTED) {
      break;
    }

    unsigned long lastRequest = conn->webServer.getLastRequestTime();
    unsigned long now = millis();

    if (ConnectivityPolicy::shouldDisconnectForInactivity(lastRequest, now,
                                                          INACTIVITY_TIMEOUT_MS)) {
      LOG_INFO(LOG_TAG, "WiFi inactive for %lu ms, auto-disconnecting",
               (unsigned long)(now - lastRequest));
      conn->disconnect();
      break;
    }
  }

  conn->inactivityTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void Connectivity::receiveGPSData(const GPSData& data) {
  if (state == WifiState::CONNECTED) {
    webServer.receiveGPSData(data);
  }
}

// ============================================
// WiFi Connection
// ============================================

struct NetworkScanResult {
  bool foundHome;
  bool foundPhone;
};

static NetworkScanResult scanForKnownNetworks() {
  int networks = WiFi.scanNetworks();
  LOG_INFO(LOG_TAG, "Available networks:");
  bool foundHome = false;
  bool foundPhone = false;

  for (int i = 0; i < networks; ++i) {
    String ssid = WiFi.SSID(i);
    LOG_INFO(LOG_TAG, "  %d: %s (%d%%)", i + 1, ssid.c_str(), WiFi.RSSI(i));
    if (ssid == home_ssid) foundHome = true;
    if (ssid == phone_ssid) foundPhone = true;
  }

  return {foundHome, foundPhone};
}

static bool connectToNetwork(const char* ssid, const char* password) {
  LOG_INFO(LOG_TAG, "Connecting to %s", ssid);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 10000;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERROR(LOG_TAG, "WiFi connection failed: network not found or wrong password.");
    return false;
  }

  LOG_INFO(LOG_TAG, "WiFi connected. IP address: %s", WiFi.localIP().toString().c_str());

  if (MDNS.begin(MDNS_HOSTNAME)) {
    LOG_INFO(LOG_TAG, "mDNS responder started: %s.local", MDNS_HOSTNAME);
  } else {
    LOG_ERROR(LOG_TAG, "Error starting mDNS. Fallback to IP: %s",
              WiFi.localIP().toString().c_str());
  }

  return true;
}

static bool startAccessPoint() {
  LOG_INFO(LOG_TAG, "No known networks found. Starting WiFi Access Point.");
  bool started = WiFi.softAP(ap_ssid, ap_password);
  if (started) {
    IPAddress apIP = WiFi.softAPIP();
    LOG_INFO(LOG_TAG, "AP started: SSID=%s, IP=%s", ap_ssid, apIP.toString().c_str());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      LOG_INFO(LOG_TAG, "mDNS responder started: %s.local", MDNS_HOSTNAME);
    } else {
      LOG_ERROR(LOG_TAG, "Error starting mDNS. Fallback to IP: %s", apIP.toString().c_str());
    }
  } else {
    LOG_ERROR(LOG_TAG, "Failed to start AP");
  }
  return started;
}

bool Connectivity::connectWifi() {
  NetworkScanResult scan = scanForKnownNetworks();

  if (scan.foundHome) {
    return connectToNetwork(home_ssid, home_password);
  } else if (scan.foundPhone) {
    return connectToNetwork(phone_ssid, phone_password);
  }

  return startAccessPoint();
}
