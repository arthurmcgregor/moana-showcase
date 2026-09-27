#pragma once

#include <stdint.h>

#include "espnow_protocol.h"

// Raw ESP-NOW transport: receives moana::Telemetry broadcasts from the boat
// unit and sends moana::Command packets back. Pure I/O — the resend/timeout
// decision logic lives in command_channel (no radio includes there).
namespace espnow_link {

// How recently a rejected frame still counts as evidence that the boat is
// there and transmitting (issue #173). Same window as link loss, and for the
// same reason: the boat broadcasts at 1 Hz, so nothing arriving for ten
// seconds means nothing is arriving — mismatched or otherwise.
constexpr uint32_t kMismatchFreshMs = 10000;

struct Stats {
  moana::Telemetry latest;
  uint32_t packet_count;
  uint32_t last_rx_ms;  // millis() of last packet, 0 = never

  // Frames this link rejected, and why (issue #173). Rejecting them is right —
  // it is what stops a layout disagreement from misparsing into plausible
  // digits — but packet_count and last_rx_ms only count frames that *passed*,
  // so on their own a rejection presents exactly as a boat that is switched
  // off or out of range. These are the only evidence the watch has otherwise,
  // and they are per reason because the reasons mean different things: a
  // version mismatch is a half-finished reflash and the fix is a laptop, not a
  // power cycle of the CoreS3; a foreign type or an unreadable length on our
  // own version is somebody else's ESP-NOW traffic on channel 1. Mirrors
  // EspnowTelemetry::RxStats on the boat.
  uint32_t version_mismatch_count;
  uint8_t last_mismatch_version;  // the version byte that frame carried
  uint32_t last_mismatch_ms;      // millis() of the last one, 0 = never
  uint32_t foreign_type_count;    // our version and size, not our packet type
  uint32_t unknown_length_count;  // our version, no struct that size

  // True while a version mismatch is recent enough to still mean "the boat is
  // right there and we cannot read it". The three fields behind this answer
  // belong to this struct, so the question is answered here rather than
  // reassembled at the call site; the caller adds only what it alone knows —
  // that nothing valid is getting through either (model::LinkState::LOST).
  bool protocolMismatchFresh(uint32_t nowMs) const {
    return version_mismatch_count > 0 &&
           nowMs - last_mismatch_ms < kMismatchFreshMs;
  }
};

bool begin();
Stats getStats();

// Broadcast a SET_RECORDING command (absolute on/off, never toggle). Stamps
// version/type/seq. Called from the LVGL timer on the main loop task.
bool sendCommand(bool desiredRecordingOn);

// Broadcast one queued Event packet (issue #139). Stamps version/type only —
// every other field belongs to the queue, which owns the seq (persistent,
// never reused, unlike the per-boot command seq) and the oldest_unacked_seq
// beside it. Build the packet with event_queue::wireEvent rather than field by
// field: the payload is one bundle on the wire, and passing it as one is what
// stops the next field added to it from widening five signatures. Called from
// the LVGL timer on the main loop task.
bool sendEvent(const moana::Event& event);

}  // namespace espnow_link
