#include "event_store_nvs.h"

#include <Arduino.h>

namespace {
constexpr const char* kNamespace = "moana-evt";
constexpr const char* kKey = "queue";
}  // namespace

bool NvsEventStore::begin() {
  ready_ = prefs_.begin(kNamespace, false);
  if (!ready_) {
    Serial.println("[evtstore] NVS open failed — queue will not survive reboot");
  }
  return ready_;
}

size_t NvsEventStore::load(uint8_t* buf, size_t cap) {
  if (!ready_) {
    return 0;
  }
  size_t stored = prefs_.getBytesLength(kKey);
  if (stored == 0 || stored > cap) {
    return 0;
  }
  return prefs_.getBytes(kKey, buf, stored);
}

bool NvsEventStore::save(const uint8_t* data, size_t len) {
  if (!ready_) {
    return false;
  }
  return prefs_.putBytes(kKey, data, len) == len;
}
