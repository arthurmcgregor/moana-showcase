// Command channel state machine for watch → boat SET_RECORDING (issue #138).
//
// Pure decision logic, split from the radio: no Arduino, no LVGL, no ESP-NOW
// includes — tick(now_ms, echoed_state) returns whether to send and what;
// espnow_link does the actual I/O. Same pattern as the device's
// RecordingStateMachine. Unit-tested in test/test_native/.
//
// Semantics (state-echo architecture):
// - request() arms an absolute SET (on/off, never toggle).
// - While pending, the command is (re)sent once per second — a lost packet
//   self-heals within ~1 s, and the boat treats duplicates as no-ops.
// - The pending command is confirmed purely by the telemetry echo: desired
//   on is confirmed by STARTING/ACTIVE, desired off by STOPPING/IDLE.
// - Without confirmation for 10 s the channel gives up and latches a timeout
//   for the UI to report; display then returns to the echoed truth.
#pragma once

#include <stdint.h>

#include "espnow_protocol.h"

namespace command_channel {

constexpr uint32_t kResendIntervalMs = 1000;
constexpr uint32_t kTimeoutMs = 10000;

class Channel {
 public:
  struct Tick {
    bool send = false;
    bool desiredOn = false;
  };

  // Ask the boat for an absolute recording state.
  // Idempotent twice over: requesting a state the echo already confirms is a
  // no-op, and re-requesting the already-pending state does not reset the
  // timeout clock. Returns whether a command is pending after the call.
  bool request(bool desiredOn, uint8_t echoedState, bool echoValid,
               uint32_t nowMs);

  // Call once per UI tick with the latest telemetry echo (echoValid = false
  // when no telemetry has ever been received). Returns what to transmit,
  // if anything.
  Tick tick(uint32_t nowMs, uint8_t echoedState, bool echoValid);

  bool pending() const { return pending_; }
  bool desiredOn() const { return desiredOn_; }

  // True once after a 10 s timeout; reading clears the latch.
  bool consumeTimeout();

 private:
  static bool confirms(bool desiredOn, uint8_t echoedState);

  bool pending_ = false;
  bool desiredOn_ = false;
  bool timedOut_ = false;
  bool sentOnce_ = false;
  uint32_t requestedAtMs_ = 0;
  uint32_t lastSendMs_ = 0;
};

}  // namespace command_channel
