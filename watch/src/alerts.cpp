#include "alerts.h"

namespace alerts {

static Cue cueFor(Fault f) {
  return f == Fault::LINK_LOST ? Cue::LINK_LOST : Cue::GPS_LOST;
}

Cue Monitor::tick(const Input& in) {
  // Fix state is only knowable while packets arrive: out of range the last
  // fix_valid freezes, so the debounce restarts from the moment the link
  // comes back rather than counting time nobody was listening.
  if (in.linkLost || in.fixValid) {
    noFix_ = false;
  } else if (!noFix_) {
    noFix_ = true;
    noFixSinceMs_ = in.nowMs;
  }

  // The fix clock runs whether or not recording is live, so pressing record
  // under a sky that has been blank for a minute alerts immediately instead
  // of after another 10 s.
  Fault now = Fault::NONE;
  if (in.armed) {
    if (in.linkLost) {
      now = Fault::LINK_LOST;
    } else if (noFix_ && in.nowMs - noFixSinceMs_ >= kGpsLostMs) {
      now = Fault::GPS_LOST;
    }
  }

  if (now != fault_) {
    fault_ = now;
    repeats_ = 0;
    lastCueMs_ = in.nowMs;
    if (now != Fault::NONE) {
      announced_ = true;
      return cueFor(now);
    }
    bool cleared = announced_ && in.armed;
    announced_ = false;
    // Recording stopping is not a recovery — disarming takes a live fault
    // away silently, it does not resolve it.
    return cleared ? Cue::CLEARED : Cue::NONE;
  }

  if (fault_ == Fault::NONE || repeats_ >= kMaxRepeats ||
      in.nowMs - lastCueMs_ < kRepeatMs) {
    return Cue::NONE;
  }
  lastCueMs_ = in.nowMs;
  repeats_++;
  return cueFor(fault_);
}

}  // namespace alerts
