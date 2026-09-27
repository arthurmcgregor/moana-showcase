// AppModel — one immutable snapshot built per UI tick (issue #138).
// Screens and the header are dumb renderers of this struct; nothing below
// ui.cpp reads espnow_link or the PMU directly. Pure header: no Arduino,
// no LVGL, no radio includes.
#pragma once

#include <stdint.h>

#include "espnow_protocol.h"
#include "stats_engine.h"

namespace model {

// Link health from telemetry packet age (thresholds per watch/CLAUDE.md).
enum class LinkState : uint8_t {
  FRESH,  // packet within 3 s
  STALE,  // 3–10 s — data demoted but shown
  LOST,   // >10 s or never received — boat state unknown, control disabled
};

constexpr uint32_t kLinkFreshMs = 3000;
constexpr uint32_t kLinkLostMs = 10000;

// The mismatch freshness window is espnow_link::kMismatchFreshMs, beside the
// counters it ages — this header stays free of radio includes, and the one
// caller that needs both (ui.cpp) already holds the Stats.

inline LinkState classifyLink(bool everReceived, uint32_t ageMs) {
  if (!everReceived || ageMs >= kLinkLostMs) {
    return LinkState::LOST;
  }
  if (ageMs >= kLinkFreshMs) {
    return LinkState::STALE;
  }
  return LinkState::FRESH;
}

struct AppModel {
  // Latest telemetry (zeroed until the first packet).
  moana::Telemetry telemetry{};
  bool haveTelemetry = false;
  uint32_t linkAgeMs = 0;  // UINT32_MAX when never received
  LinkState link = LinkState::LOST;

  // Recording state as echoed by the boat — never a local assumption.
  uint8_t recordingEcho = moana::RECORDING_IDLE;  // moana::RecordingStatus
  bool recording() const { return recordingEcho == moana::RECORDING_ACTIVE; }

  // Sailing mode as echoed by the boat (moana::SailingMode) — same
  // state-echo pattern as recording, never the last local button press.
  uint8_t modeEcho = moana::MODE_UNKNOWN;

  // The mode the watch last asked for and whether that request is still
  // in flight (issue #146). Display truth stays modeEcho; these only let the
  // picker mark a queued-but-unconfirmed row, so a tap made out of range
  // visibly registers without the UI pretending the boat has applied it.
  uint8_t modeRequested = moana::MODE_UNKNOWN;
  bool modeRequestPending = false;

  // Command channel status (watch → boat SET_RECORDING).
  bool commandPending = false;
  bool commandDesiredOn = false;
  bool commandTimedOut = false;  // held true briefly after a 10 s timeout

  // Unacked events in the store-and-forward queue (issue #139).
  int queuePending = 0;

  // A protocol-version mismatch seen within kMismatchFreshMs (issue #173).
  // Deliberately NOT a fourth LinkState: for every screen that greys out its
  // digits this IS a lost link, and widening the enum would have each of
  // those `== LinkState::LOST` checks quietly stop firing. It is extra
  // information about a lost link, not a different kind of link — the boat is
  // transmitting, we cannot read it, and no amount of waiting will help.
  bool protocolMismatch = false;
  uint8_t mismatchVersion = 0;  // what the boat said it was speaking

  // Read-only view of the stats engine (issue #142) — session/leg stats the
  // Stats screen browses and the Speed screen's secondary value reads. A
  // pointer rather than a copy: the legs ring is too big to snapshot per
  // tick, and the engine only mutates inside the UI tick, before any screen
  // renders. Never null after ui::begin().
  const stats_engine::Engine* stats = nullptr;

  // UTC from the timebase (issue #144): the RTC's time until a packet
  // arrives, then telemetry UTC extrapolated by millis. 0 = time unknown,
  // which is the only case the clock renders as `--:--`. Deliberately not
  // telemetry.utc_seconds — a link-derived clock goes blank the moment the
  // boat does, and the watch has an RTC precisely so it doesn't have to.
  uint32_t utcSeconds = 0;

  int batteryPercent = 0;
  bool charging = false;
  uint32_t nowMs = 0;
};

}  // namespace model
