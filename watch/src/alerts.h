// Fault alerts (issue #162): buzz the wrist when the boat loses its GPS fix
// or when the watch walks out of ESP-NOW range. Pure state machine — no
// Arduino, LVGL or radio includes — so it is unit-tested natively and ui.cpp
// is left holding only the haptic call.
//
// The point of these alerts is to reach you when you are NOT looking at the
// watch, so ui.cpp ticks this before the sleep gate: a dark screen must still
// buzz.
#pragma once

#include <stdint.h>

namespace alerts {

// What is wrong right now. LINK_LOST outranks GPS_LOST: with no packets
// arriving the last fix_valid is stale, so the boat's GPS state is unknown
// rather than known-bad.
enum class Fault : uint8_t {
  NONE = 0,
  LINK_LOST = 1,  // no telemetry for model::kLinkLostMs
  GPS_LOST = 2,   // boat reporting fix_valid == 0 for kGpsLostMs
};

// One haptic's worth of output per tick. NONE the vast majority of ticks.
enum class Cue : uint8_t {
  NONE = 0,
  LINK_LOST,
  GPS_LOST,
  CLEARED,  // a fault that had been announced has gone away
};

// The boat declares its own fix stale after 3 s without a GPS_DATA event
// (device/include/app.h ESPNOW_FIX_FRESH_MS), so fix_valid flickers under a
// marginal sky. Alert only on a loss that holds — the same 10 s the link
// gets before it is called lost, so the two faults have one patience.
constexpr uint32_t kGpsLostMs = 10000;

// A fault you slept through is worth repeating, a fault you have decided to
// live with is not: re-cue every kRepeatMs up to kMaxRepeats, then stay
// quiet until the fault clears and happens again. Uncapped repeats would
// buzz all night against a boat that is simply switched off.
constexpr uint32_t kRepeatMs = 60000;
constexpr uint8_t kMaxRepeats = 5;

struct Input {
  uint32_t nowMs = 0;
  // Last-known recording echo is ACTIVE. Alerts are armed by recording and
  // nothing else: a watch sitting on the charger beside a powered-down boat
  // is permanently "out of range" and must stay silent. Deliberately the
  // *last known* echo — losing the link mid-session leaves it ACTIVE, which
  // is exactly the case the link alert exists for.
  bool armed = false;
  bool linkLost = false;  // model::LinkState::LOST
  bool fixValid = false;  // telemetry.fix_valid != 0
};

class Monitor {
 public:
  // One call per UI tick. Returns the cue to play, or Cue::NONE.
  Cue tick(const Input& in);

  Fault fault() const { return fault_; }

 private:
  Fault fault_ = Fault::NONE;
  bool announced_ = false;  // fault_ has buzzed at least once
  uint32_t lastCueMs_ = 0;
  uint8_t repeats_ = 0;
  bool noFix_ = false;
  uint32_t noFixSinceMs_ = 0;
};

}  // namespace alerts
