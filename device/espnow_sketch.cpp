// Standalone ESP-NOW telemetry sender for testing the watch link
// (issue #133). Reads the GPS module directly (same wiring as the main
// firmware: UART2, RX=18, TX=17) and broadcasts moana::Telemetry at 1Hz.
// Build/flash with: pio run -e espnow-sketch --target upload
//
// Heel/trim/heading are sent as zeros here — this sketch skips the IMU to
// stay minimal; integrating ESP-NOW into the main firmware is a later slice.
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "espnow_protocol.h"

static TinyGPSPlus gps;
static HardwareSerial gpsSerial(2);
static uint16_t seq = 0;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(115200, SERIAL_8N1, 18, 17);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(moana::kEspNowChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed");
    while (true) {
      delay(1000);
    }
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, moana::kBroadcastAddress, 6);
  peer.channel = moana::kEspNowChannel;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("esp_now_add_peer failed");
  }

  Serial.printf("Broadcasting telemetry on channel %u, MAC %s\n",
                moana::kEspNowChannel, WiFi.macAddress().c_str());
}

void loop() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  static uint32_t lastSend = 0;
  if (millis() - lastSend < 1000) {
    return;
  }
  lastSend = millis();

  moana::Telemetry t = {};
  t.version = moana::kProtocolVersion;
  t.type = moana::PACKET_TELEMETRY;
  t.seq = seq++;
  t.fix_valid = gps.location.isValid() ? 1 : 0;
  if (t.fix_valid) {
    t.lat = gps.location.lat();
    t.lng = gps.location.lng();
  }
  t.sog_knots = gps.speed.isValid() ? gps.speed.knots() : 0.0f;
  t.cog_deg = gps.course.isValid() ? gps.course.deg() : 0.0f;
  t.altitude_m = gps.altitude.isValid() ? gps.altitude.meters() : 0.0f;
  t.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0f;
  t.satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
  if (gps.date.isValid() && gps.time.isValid()) {
    t.utc_seconds = moana::utcEpochFromGps(gps.date.value(), gps.time.value());
  }

  esp_err_t err = esp_now_send(moana::kBroadcastAddress,
                               reinterpret_cast<const uint8_t*>(&t), sizeof(t));
  Serial.printf("seq=%u fix=%u sats=%u sog=%.1fkn send=%s\n", t.seq,
                t.fix_valid, t.satellites, t.sog_knots,
                err == ESP_OK ? "ok" : "FAIL");
}
