#include "espnow_link.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

namespace espnow_link {

static Stats stats = {};
// Receive callback runs on the WiFi task; guard the shared struct.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;

  // The version gate, before any length or type is read — the rule and the
  // reason live in moana::checkFrameVersion, once, for both firmwares.
  uint8_t version = 0;
  switch (moana::checkFrameVersion(data, len, &version)) {
    case moana::FrameVersion::OK:
      break;
    case moana::FrameVersion::TOO_SHORT:
      portENTER_CRITICAL(&mux);
      stats.unknown_length_count++;
      portEXIT_CRITICAL(&mux);
      return;
    case moana::FrameVersion::MISMATCH:
      portENTER_CRITICAL(&mux);
      stats.version_mismatch_count++;
      stats.last_mismatch_version = version;
      stats.last_mismatch_ms = millis();
      portEXIT_CRITICAL(&mux);
      return;
  }

  // Our version, but nothing the boat sends is this long: a packet type this
  // firmware predates, or a frame that arrived damaged.
  if (len != sizeof(moana::Telemetry)) {
    portENTER_CRITICAL(&mux);
    stats.unknown_length_count++;
    portEXIT_CRITICAL(&mux);
    return;
  }
  moana::Telemetry incoming;
  memcpy(&incoming, data, sizeof(incoming));
  if (incoming.type != moana::PACKET_TELEMETRY) {
    // Our version and our size, someone else's packet type — the watch only
    // ever expects Telemetry, so this is another sender on channel 1.
    portENTER_CRITICAL(&mux);
    stats.foreign_type_count++;
    portEXIT_CRITICAL(&mux);
    return;
  }
  portENTER_CRITICAL(&mux);
  stats.latest = incoming;
  stats.packet_count++;
  stats.last_rx_ms = millis();
  portEXIT_CRITICAL(&mux);
}

bool begin() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(moana::kEspNowChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] init failed");
    return false;
  }
  esp_now_register_recv_cb(onReceive);

  // Sending (commands) requires the broadcast peer; receiving does not.
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, moana::kBroadcastAddress, 6);
  peer.channel = 0;  // 0 = follow the radio's current channel
  peer.encrypt = false;
  esp_err_t err = esp_now_add_peer(&peer);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[espnow] add_peer failed: %d\n", err);
    return false;
  }

  Serial.printf("[espnow] listening on channel %u, MAC %s\n",
                moana::kEspNowChannel, WiFi.macAddress().c_str());
  return true;
}

Stats getStats() {
  portENTER_CRITICAL(&mux);
  Stats copy = stats;
  portEXIT_CRITICAL(&mux);
  return copy;
}

bool sendCommand(bool desiredRecordingOn) {
  // Only touched from the LVGL timer (main loop task) — no lock needed.
  static uint16_t seq = 0;

  moana::Command cmd = {};
  cmd.version = moana::kProtocolVersion;
  cmd.type = moana::PACKET_COMMAND;
  cmd.seq = seq++;
  cmd.desired_recording = desiredRecordingOn ? 1 : 0;

  esp_err_t err = esp_now_send(moana::kBroadcastAddress,
                               reinterpret_cast<const uint8_t*>(&cmd),
                               sizeof(cmd));
  if (err != ESP_OK) {
    Serial.printf("[espnow] command send failed: %d\n", err);
    return false;
  }
  return true;
}

bool sendEvent(const moana::Event& event) {
  moana::Event ev = event;
  ev.version = moana::kProtocolVersion;
  ev.type = moana::PACKET_EVENT;

  esp_err_t err = esp_now_send(moana::kBroadcastAddress,
                               reinterpret_cast<const uint8_t*>(&ev),
                               sizeof(ev));
  if (err != ESP_OK) {
    // Failures here are non-fatal: the queue resends all unacked 1/s.
    Serial.printf("[espnow] event send failed: %d\n", err);
    return false;
  }
  return true;
}

}  // namespace espnow_link
