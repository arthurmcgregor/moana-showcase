#include "command_channel.h"

namespace command_channel {

bool Channel::confirms(bool desiredOn, uint8_t echoedState) {
  if (desiredOn) {
    // STARTING already proves the boat received the command; ACTIVE is the
    // steady state (or a device-side start that makes ours redundant).
    return echoedState == moana::RECORDING_STARTING ||
           echoedState == moana::RECORDING_ACTIVE;
  }
  return echoedState == moana::RECORDING_STOPPING ||
         echoedState == moana::RECORDING_IDLE;
}

bool Channel::request(bool desiredOn, uint8_t echoedState, bool echoValid,
                      uint32_t nowMs) {
  if (pending_) {
    if (desiredOn_ == desiredOn) {
      // Same request already in flight — don't reset the timeout clock.
      return true;
    }
    // Opposite of the in-flight command: replace it outright, even if the
    // echo currently shows the new desired state — the old command may have
    // already reached the boat and be about to flip it. Sending the new SET
    // is safe either way (idempotent no-op if the boat never moved).
  } else if (echoValid && confirms(desiredOn, echoedState)) {
    // Already in (or moving to) the desired state — nothing to do.
    return false;
  }
  pending_ = true;
  desiredOn_ = desiredOn;
  sentOnce_ = false;
  requestedAtMs_ = nowMs;
  return true;
}

Channel::Tick Channel::tick(uint32_t nowMs, uint8_t echoedState,
                            bool echoValid) {
  if (!pending_) {
    return {};
  }

  // A pending command must hit the air at least once before the echo can
  // confirm it — otherwise a command that replaced its opposite mid-flight
  // (echo still showing the old state, which "confirms" the new desire)
  // would be swallowed without ever being sent.
  if (sentOnce_ && echoValid && confirms(desiredOn_, echoedState)) {
    pending_ = false;
    return {};
  }

  if (nowMs - requestedAtMs_ >= kTimeoutMs) {
    pending_ = false;
    timedOut_ = true;
    return {};
  }

  if (!sentOnce_ || nowMs - lastSendMs_ >= kResendIntervalMs) {
    sentOnce_ = true;
    lastSendMs_ = nowMs;
    Tick out;
    out.send = true;
    out.desiredOn = desiredOn_;
    return out;
  }

  return {};
}

bool Channel::consumeTimeout() {
  bool was = timedOut_;
  timedOut_ = false;
  return was;
}

}  // namespace command_channel
