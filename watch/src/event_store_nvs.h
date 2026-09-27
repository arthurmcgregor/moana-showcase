#pragma once

#include <Preferences.h>

#include "event_queue.h"

// Production event_queue::Store adapter on NVS (issue #139).
//
// NVS over a FATFS file deliberately: the watch's filesystem is FAT (see
// platformio.ini), which is not power-fail safe for in-place rewrites,
// while NVS blob commits are atomic and wear-levelled — exactly right for
// a <512-byte blob rewritten on the (rare) mode press. Same Preferences
// pattern the device uses for IMU calibration (`magcal` namespace).
class NvsEventStore : public event_queue::Store {
 public:
  // Opens the NVS namespace; returns false if NVS is unusable (the queue
  // then still works, just without reboot persistence).
  bool begin();

  size_t load(uint8_t* buf, size_t cap) override;
  bool save(const uint8_t* data, size_t len) override;

 private:
  Preferences prefs_;
  bool ready_ = false;
};
