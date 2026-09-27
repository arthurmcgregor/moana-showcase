#pragma once

#include "types.h"

// Pure fix-state policy for GPS — no hardware, no timers, no Arduino. Extracted
// from GPS::healthTask so the staleness rule is exercisable in the native
// suite (F5). The health task supplies wall-clock age; the rule lives here.
namespace GpsPolicy {

// A LOCKED fix decays to LOST once its most recent update is older than the
// stale threshold. Any other state is left untouched — reacquisition back to
// LOCKED is driven by fresh GPS_DATA on the data path, not by this check.
// Returns the state the health check should move to (== current when no
// transition is warranted).
inline GPSState healthCheck(GPSState current, unsigned long fixAgeMs,
                            unsigned long staleThresholdMs) {
  if (current == GPSState::LOCKED && fixAgeMs > staleThresholdMs) {
    return GPSState::LOST;
  }
  return current;
}

}  // namespace GpsPolicy
