#include "connectivity/espnow_telemetry.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

#include "utils/logger.h"

static const char* LOG_TAG = "EspnowTelemetry";

EspnowTelemetry* EspnowTelemetry::instance_ = nullptr;

void EspnowTelemetry::begin(TelemetrySnapshotFn compose, CommandFn onCommand,
                            EventFn onEvent) {
  this->compose = compose;
  this->onCommand = onCommand;
  this->onEvent = onEvent;
  instance_ = this;
  initRadio();
  xTaskCreate(EspnowTelemetry::sendTask, "EspnowSend", 4096, this, 1, nullptr);
}

bool EspnowTelemetry::initRadio() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(moana::kEspNowChannel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    LOG_ERROR(LOG_TAG, "esp_now_init failed");
    return false;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, moana::kBroadcastAddress, 6);
  peer.channel = 0;  // 0 = follow the radio's current channel
  peer.encrypt = false;
  esp_err_t err = esp_now_add_peer(&peer);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    LOG_ERROR(LOG_TAG, "esp_now_add_peer failed: %d", err);
    return false;
  }

  // Registered here (not just in begin) so restart() re-arms receive along
  // with the peer — esp_now_deinit drops both.
  err = esp_now_register_recv_cb(EspnowTelemetry::onReceiveStatic);
  if (err != ESP_OK) {
    LOG_ERROR(LOG_TAG, "esp_now_register_recv_cb failed: %d", err);
    return false;
  }

  radioReady = true;
  LOG_INFO(LOG_TAG, "Broadcasting telemetry on channel %u",
           moana::kEspNowChannel);
  return true;
}

void EspnowTelemetry::restart() {
  radioReady = false;
  esp_now_deinit();
  initRadio();
}

EspnowTelemetry::RxStats EspnowTelemetry::rxStats() const {
  RxStats out;
  out.versionMismatches = versionMismatches.load();
  out.lastMismatchVersion = lastMismatchVersion.load();
  out.foreignTypes = foreignTypes.load();
  out.unknownLengths = unknownLengths.load();
  return out;
}

void EspnowTelemetry::noteVersionMismatch(uint8_t version) {
  versionMismatches++;
  lastMismatchVersion = version;

  // Says the one thing the silence could not (issue #173): the watch is right
  // there and transmitting, and the two of us disagree about the layout.
  unsigned long now = millis();
  unsigned long last = lastMismatchLogMs.load();
  if (last != 0 && now - last < MISMATCH_LOG_INTERVAL_MS) {
    return;
  }
  lastMismatchLogMs = now;
  LOG_WARN(LOG_TAG,
           "Rejected a v%u frame (we speak v%u) — %u so far. The watch needs "
           "reflashing: cd watch && pio run -t upload",
           version, moana::kProtocolVersion,
           (unsigned)rxStats().versionMismatches);
}

void EspnowTelemetry::noteStrayFrame(bool foreignType) {
  if (foreignType) {
    foreignTypes++;
  } else {
    unknownLengths++;
  }

  // Quieter than the version warning on purpose: our own version with a type
  // or a length we have no use for is somebody else's ESP-NOW traffic on
  // channel 1, not a fault on this boat. It is still counted and still said,
  // because the alternative is the silence issue #173 is about — and because
  // a rising count here while telemetry looks fine is how a shared channel
  // announces itself.
  unsigned long now = millis();
  unsigned long last = lastStrayLogMs.load();
  if (last != 0 && now - last < STRAY_LOG_INTERVAL_MS) {
    return;
  }
  lastStrayLogMs = now;
  RxStats s = rxStats();
  LOG_DEBUG(LOG_TAG,
            "Ignored frames on our own version: %u of a foreign type, %u of an "
            "unreadable length",
            (unsigned)s.foreignTypes, (unsigned)s.unknownLengths);
}

void EspnowTelemetry::onReceiveStatic(const uint8_t* mac, const uint8_t* data,
                                      int len) {
  (void)mac;
  EspnowTelemetry* self = instance_;
  if (self == nullptr) {
    return;
  }

  // The version gate, before any length or type is read — the rule and the
  // reason live in moana::checkFrameVersion, once, for both firmwares.
  uint8_t version = 0;
  switch (moana::checkFrameVersion(data, len, &version)) {
    case moana::FrameVersion::OK:
      break;
    case moana::FrameVersion::TOO_SHORT:
      self->noteStrayFrame(false);
      return;
    case moana::FrameVersion::MISMATCH:
      self->noteVersionMismatch(version);
      return;
  }

  // Command and Event have distinct wire sizes; dispatch on size, then
  // confirm the type. Both callbacks run on the WiFi task and post onto App's
  // event queue so AppController serialises them with everything else.
  if (len == (int)sizeof(moana::Command)) {
    moana::Command cmd;
    memcpy(&cmd, data, sizeof(cmd));
    if (cmd.type != moana::PACKET_COMMAND) {
      self->noteStrayFrame(true);
      return;
    }
    if (self->onCommand) {
      self->onCommand(cmd.desired_recording != 0);
    }
    return;
  }

  if (len == (int)sizeof(moana::Event)) {
    moana::Event ev;
    memcpy(&ev, data, sizeof(ev));
    if (ev.type != moana::PACKET_EVENT) {
      self->noteStrayFrame(true);
      return;
    }
    if (self->onEvent) {
      self->onEvent(ev);
    }
    return;
  }

  // Our version byte, but no watch->boat struct is this long — a packet type
  // this firmware predates, or a frame that arrived damaged.
  self->noteStrayFrame(false);
}

void EspnowTelemetry::sendOnce() {
  moana::Telemetry t = compose ? compose() : moana::Telemetry{};
  t.version = moana::kProtocolVersion;
  t.type = moana::PACKET_TELEMETRY;
  t.seq = seq++;

  esp_err_t err = esp_now_send(moana::kBroadcastAddress,
                               reinterpret_cast<const uint8_t*>(&t), sizeof(t));
  if (err != ESP_OK) {
    // Expected while Connectivity holds the radio in AP mode; keep quiet
    // beyond debug level.
    LOG_DEBUG(LOG_TAG, "esp_now_send failed: %d", err);
  }
}

void EspnowTelemetry::sendTask(void* pvParameters) {
  EspnowTelemetry* self = static_cast<EspnowTelemetry*>(pvParameters);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    if (self->radioReady) {
      self->sendOnce();
    }
  }
}
