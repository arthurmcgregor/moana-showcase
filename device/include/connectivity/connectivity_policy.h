#pragma once

// Pure connectivity policy — no WiFi, no tasks, no Arduino. Extracted from
// Connectivity::inactivityTask so the idle-disconnect rule is exercisable in
// the native suite (F5). The task supplies the clock readings; the decision
// lives here.
namespace ConnectivityPolicy {

// Auto-disconnect once the web server has been idle for at least the timeout.
// lastRequestMs == 0 means "no request has ever been observed": the timer is
// seeded on connect so this should not normally occur, but a zero must never
// be read as an ancient timestamp that trips an immediate disconnect.
inline bool shouldDisconnectForInactivity(unsigned long lastRequestMs,
                                          unsigned long nowMs,
                                          unsigned long timeoutMs) {
  return lastRequestMs > 0 && (nowMs - lastRequestMs) >= timeoutMs;
}

}  // namespace ConnectivityPolicy
