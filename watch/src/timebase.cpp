#include "timebase.h"

namespace timebase {

static uint32_t refUtc = 0;   // 0 = no reference ever seen
static uint32_t refAtMs = 0;  // millis() when refUtc was observed
static Clock* clock_ = nullptr;
// Whether the hardware is known to be holding a time this firmware wrote or
// read. False until it is, and false again the moment a write is refused —
// which is the only thing that gets the write retried, because the drift check
// below cannot see the hardware at all (issue #144).
static bool clockDisciplined = false;

static uint32_t absDiff(uint32_t a, uint32_t b) {
  return a > b ? a - b : b - a;
}

void begin(Clock* clock, uint32_t nowMs) {
  clock_ = clock;
  if (clock_ == nullptr) {
    return;
  }
  uint32_t held = clock_->readUtc();
  if (held < kPlausibleEpoch) {
    return;  // nothing real in there — stay at "time unknown"
  }
  refUtc = held;
  refAtMs = nowMs;
  clockDisciplined = true;  // it just told us what it holds
}

void feed(uint32_t utcSeconds, uint32_t refMs) {
  if (utcSeconds == 0) {
    return;  // sender had no GPS time — not a reference
  }
  // Measured against the estimate this packet is about to replace, so the
  // comparison is "was the clock right?", not "is it right now?".
  uint32_t estimate = now(refMs);
  bool drifted = estimate == 0 ||
                 absDiff(estimate, utcSeconds) >= kDriftThresholdSeconds;

  refUtc = utcSeconds;
  refAtMs = refMs;

  if (clock_ == nullptr) {
    return;
  }
  // `drifted` is measured against the RAM estimate, which after the first
  // write always agrees with telemetry — so on its own it would ask the
  // hardware to be rewritten exactly never, and a write that did not land
  // would stay unlanded for the rest of the boot with the wrist showing a
  // time it believes and the RTC holding one it doesn't (issue #144).
  // clockDisciplined is the other half: an unconfirmed write is retried on the
  // next packet, once a second, until the hardware takes it.
  if (drifted || !clockDisciplined) {
    clockDisciplined = clock_->writeUtc(utcSeconds);
  }
}

uint32_t now(uint32_t nowMs) {
  if (refUtc == 0) {
    return 0;  // time unknown — protocol sentinel
  }
  return refUtc + (uint32_t)(nowMs - refAtMs) / 1000;
}

void reset() {
  refUtc = 0;
  refAtMs = 0;
  clock_ = nullptr;
  clockDisciplined = false;
}

}  // namespace timebase
