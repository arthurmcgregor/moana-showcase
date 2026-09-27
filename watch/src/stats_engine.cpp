#include "stats_engine.h"

#include <math.h>

namespace stats_engine {

static bool isRecording(uint8_t recordingState) {
  return recordingState == moana::RECORDING_STARTING ||
         recordingState == moana::RECORDING_ACTIVE;
}

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Distance-derived average: metres over milliseconds, in knots.
static float avgKnots(uint32_t distanceM, uint32_t elapsedMs) {
  if (elapsedMs == 0) {
    return 0;
  }
  return ((float)distanceM * 1000.0f / (float)elapsedMs) * kMpsToKnots;
}

void Engine::tick(const Input& in) {
  nowMs_ = in.nowMs;
  if (!in.packet) {
    return;  // elapsed keeps counting through views; state only moves on packets
  }

  const bool recording = isRecording(in.recordingState);

  // --- session lifecycle (echo-driven) ---
  if (recording && !wasRecording_) {
    // Not-recording -> recording. If this is the very first packet we ever
    // heard, the boat was already mid-session (watch reboot / walked into
    // range): adopt it with a zero baseline so the cumulative echo restores
    // the full session distance. Otherwise it's a fresh start observed live:
    // baseline on the current echo (the device resets it to 0 at the
    // IDLE->ACTIVE transition; if we baselined on a stale held value the
    // re-base guard below corrects within one packet).
    startSession(in, packetSeen_ ? in.cumulativeDistanceM : 0);
  } else if (!recording && wasRecording_ && live_) {
    freezeSession(in);
  }
  wasRecording_ = recording;

  if (live_) {
    // Re-base guard: the echo dropped below a stored baseline (the device
    // accumulator restarted under us, e.g. we baselined during STARTING on
    // the previous session's held value). Slide the baseline down rather
    // than underflowing.
    if (in.cumulativeDistanceM < sessionStartCumM_) {
      sessionStartCumM_ = in.cumulativeDistanceM;
    }
    if (in.cumulativeDistanceM < currentLeg().startCumM) {
      currentLeg().startCumM = in.cumulativeDistanceM;
    }
    lastCumM_ = in.cumulativeDistanceM;

    // --- leg cutting (echo-driven) ---
    Leg& cur = currentLeg();
    if (in.currentMode != moana::MODE_UNKNOWN && in.currentMode != cur.mode) {
      if (cur.mode == moana::MODE_UNKNOWN) {
        // First classification after boot labels the running leg — the leg
        // was never really a different mode, so this is not a cut.
        cur.mode = in.currentMode;
      } else {
        cutLeg(in.currentMode, in);
      }
    } else if (lastAck_ != moana::kEventSeqNone &&
               in.eventSeqAcked != moana::kEventSeqNone &&
               in.eventSeqAcked != lastAck_) {
      // Ack frontier advanced with an unchanged echoed mode: the boat
      // applied event(s) that didn't move the echo — a same-mode re-assert
      // (PWR-click manual cut, or the future picker in #146).
      cutLeg(cur.mode, in);
    }

    // --- speed sample pipeline ---
    ingestSample(in);
  }

  lastMode_ = in.currentMode;
  lastAck_ = in.eventSeqAcked;
  packetSeen_ = true;
}

void Engine::startSession(const Input& in, uint32_t baselineCumM) {
  live_ = true;
  sessionExists_ = true;
  sessionStartMs_ = in.nowMs;
  sessionStartCumM_ = baselineCumM;
  sessionFrozenElapsedMs_ = 0;
  sessionFrozenDistanceM_ = 0;
  sessionMaxKn_ = 0;
  sessionHasMax_ = false;
  sessionBest10sKn_ = 0;
  sessionHasBest10s_ = false;

  legHead_ = 0;
  legCount_ = 0;
  totalLegs_ = 0;
  pushLeg(in.currentMode, in.nowMs, baselineCumM);

  lastCumM_ = in.cumulativeDistanceM;
  // Sync the ack frontier to this packet: an event acked in the very packet
  // that starts the session (mode press racing the recording start) must not
  // read as a same-mode re-assert and cut a zero-length leg 2.
  lastAck_ = in.eventSeqAcked;
  resetPipeline();
}

void Engine::freezeSession(const Input& in) {
  live_ = false;
  sessionFrozenElapsedMs_ = in.nowMs - sessionStartMs_;
  sessionFrozenDistanceM_ = lastCumM_ - sessionStartCumM_;

  Leg& cur = currentLeg();
  cur.frozenElapsedMs = in.nowMs - cur.startMs;
  cur.frozenDistanceM = lastCumM_ - cur.startCumM;
  cur.closed = true;
}

void Engine::pushLeg(uint8_t mode, uint32_t startMs, uint32_t startCumM) {
  if (legCount_ == kMaxLegs) {
    // Bounded history: the oldest leg falls off; numbering keeps counting.
    legHead_ = (legHead_ + 1) % kMaxLegs;
    legCount_--;
  }
  legCount_++;
  totalLegs_++;
  Leg& leg = currentLeg();
  leg = Leg{};
  leg.number = totalLegs_;
  leg.mode = mode;
  leg.startMs = startMs;
  leg.startCumM = startCumM;
}

void Engine::cutLeg(uint8_t newMode, const Input& in) {
  Leg& cur = currentLeg();

  // Mis-press debounce (issue #161): a leg only kMinLegMs old is not a leg the
  // user meant to sail — it is the mode they tapped by mistake and have just
  // corrected. Relabel it in place instead of cutting: the leg keeps its
  // number, its start and its trackers (same water, same samples), so nothing
  // has to be merged back and the wrist never shows a five-second leg.
  //
  // Retroactive on purpose. Holding the press for the window instead would
  // silence the haptic and the mode chip for 20 s, and the boat's sidecar
  // would still carry the mis-press. Nothing upstream is debounced — the
  // server collapses the same burst on import (events_jsonl.MODE_DEBOUNCE_SECONDS)
  // with the same rule, first press's time carrying the last press's mode.
  //
  // A same-mode re-assert lands here too (the PWR-click manual cut), so a
  // second cut inside the window is swallowed: newMode == cur.mode makes the
  // relabel a no-op. Deliberate, and the server does the same.
  if (in.nowMs - cur.startMs < kMinLegMs) {
    cur.mode = newMode;
    return;
  }

  cur.frozenElapsedMs = in.nowMs - cur.startMs;
  cur.frozenDistanceM = lastCumM_ - cur.startCumM;
  cur.closed = true;
  pushLeg(newMode, in.nowMs, in.cumulativeDistanceM);
  // The median filter and rolling window deliberately survive the cut: the
  // speed stream is continuous across a mode change. The new leg's best-10s
  // stays empty until a full window fits inside it (oldest-sample check in
  // ingestCleaned).
}

bool Engine::resetSession() {
  if (!live_) {
    return false;
  }
  // Restart in place from the last packet's snapshot; the device keeps its
  // accumulator running, so the new baseline is simply the current echo.
  sessionStartMs_ = nowMs_;
  sessionStartCumM_ = lastCumM_;
  sessionMaxKn_ = 0;
  sessionHasMax_ = false;
  sessionBest10sKn_ = 0;
  sessionHasBest10s_ = false;

  legHead_ = 0;
  legCount_ = 0;
  totalLegs_ = 0;
  pushLeg(lastMode_, nowMs_, lastCumM_);
  resetPipeline();
  return true;
}

void Engine::resetPipeline() {
  haveAccepted_ = false;
  medianNext_ = 0;
  medianFill_ = 0;
  windowHead_ = 0;
  windowCount_ = 0;
}

void Engine::ingestSample(const Input& in) {
  // Gate 1: only valid fixes with a usable dilution. hdop == 0 means the
  // boat never populated it — treat as unusable rather than "perfect".
  if (!in.fixValid || in.hdop <= 0.0f || in.hdop >= kHdopMax) {
    return;
  }

  // Gate 2: physically-impossible acceleration relative to the last
  // ACCEPTED sample, scaled by the time since it (so an isolated spike is
  // rejected, while a genuine step change re-syncs within a few seconds as
  // the allowed delta grows).
  if (haveAccepted_) {
    float dtS = (float)(in.nowMs - lastAcceptMs_) / 1000.0f;
    float allowedKn = clampf(kAccelKnPerSec * dtS, kAccelFloorKn, kAccelCeilKn);
    if (fabsf(in.sogKnots - lastAcceptedKn_) > allowedKn) {
      return;
    }
  }
  haveAccepted_ = true;
  lastAcceptedKn_ = in.sogKnots;
  lastAcceptMs_ = in.nowMs;

  // Median-of-5 over accepted samples.
  median_[medianNext_] = in.sogKnots;
  medianNext_ = (medianNext_ + 1) % kMedianTaps;
  if (medianFill_ < kMedianTaps) {
    medianFill_++;
  }
  if (medianFill_ < kMedianMinTaps) {
    return;  // not enough taps yet to trust a median
  }
  ingestCleaned(medianOfBuffer(), in.nowMs);
}

float Engine::medianOfBuffer() const {
  float sorted[kMedianTaps];
  for (size_t i = 0; i < medianFill_; i++) {
    sorted[i] = median_[i];
  }
  // Insertion sort — at most 5 elements.
  for (size_t i = 1; i < medianFill_; i++) {
    float v = sorted[i];
    size_t j = i;
    while (j > 0 && sorted[j - 1] > v) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = v;
  }
  if (medianFill_ % 2 == 1) {
    return sorted[medianFill_ / 2];
  }
  return (sorted[medianFill_ / 2 - 1] + sorted[medianFill_ / 2]) / 2.0f;
}

void Engine::ingestCleaned(float kn, uint32_t nowMs) {
  // Strict monotonic MAX of the cleaned stream.
  Leg& cur = currentLeg();
  if (!sessionHasMax_ || kn > sessionMaxKn_) {
    sessionMaxKn_ = kn;
  }
  sessionHasMax_ = true;
  if (!cur.hasMax || kn > cur.maxKn) {
    cur.maxKn = kn;
  }
  cur.hasMax = true;

  // Rolling 10 s window over received samples with timestamps.
  if (windowCount_ == kWindowCap) {
    windowHead_ = (windowHead_ + 1) % kWindowCap;
    windowCount_--;
  }
  window_[(windowHead_ + windowCount_) % kWindowCap] = {nowMs, kn};
  windowCount_++;

  // Evict samples older than the window.
  while (windowCount_ > 0 && nowMs - window_[windowHead_].tMs > kWindowMs) {
    windowHead_ = (windowHead_ + 1) % kWindowCap;
    windowCount_--;
  }

  // Candidate check: the window must span ~10 s of real time with no
  // telemetry gap inside it — a window across a gap yields NO candidate
  // rather than a synthetic one.
  const WindowSample& oldest = window_[windowHead_];
  if (nowMs - oldest.tMs < kWindowMinSpanMs) {
    return;  // first ~10 s of a session (or after a reset): no value yet
  }
  float sum = 0;
  uint32_t prevT = oldest.tMs;
  for (size_t i = 0; i < windowCount_; i++) {
    const WindowSample& s = window_[(windowHead_ + i) % kWindowCap];
    if (s.tMs - prevT > kWindowMaxGapMs) {
      return;  // gap inside the window — no candidate
    }
    prevT = s.tMs;
    sum += s.kn;
  }
  float mean = sum / (float)windowCount_;

  if (!sessionHasBest10s_ || mean > sessionBest10sKn_) {
    sessionBest10sKn_ = mean;
  }
  sessionHasBest10s_ = true;

  // The leg only owns windows that fit entirely inside it — the first 10 s
  // of a leg have no best-10s value.
  if (oldest.tMs >= cur.startMs) {
    if (!cur.hasBest10s || mean > cur.best10sKn) {
      cur.best10sKn = mean;
    }
    cur.hasBest10s = true;
  }
}

SessionView Engine::session() const {
  SessionView v;
  v.exists = sessionExists_;
  v.live = live_;
  if (!sessionExists_) {
    return v;
  }
  v.elapsedMs = live_ ? nowMs_ - sessionStartMs_ : sessionFrozenElapsedMs_;
  v.distanceM = live_ ? lastCumM_ - sessionStartCumM_ : sessionFrozenDistanceM_;
  v.avgKn = avgKnots(v.distanceM, v.elapsedMs);
  v.maxKn = sessionMaxKn_;
  v.hasMax = sessionHasMax_;
  v.best10sKn = sessionBest10sKn_;
  v.hasBest10s = sessionHasBest10s_;
  return v;
}

LegView Engine::leg(size_t idx) const {
  LegView v;
  if (idx >= legCount_) {
    return v;
  }
  const Leg& leg = legAt(idx);
  v.number = leg.number;
  v.mode = leg.mode;
  if (leg.closed) {
    v.elapsedMs = leg.frozenElapsedMs;
    v.distanceM = leg.frozenDistanceM;
  } else {
    v.elapsedMs = nowMs_ - leg.startMs;
    v.distanceM = lastCumM_ - leg.startCumM;
  }
  v.avgKn = avgKnots(v.distanceM, v.elapsedMs);
  v.maxKn = leg.maxKn;
  v.hasMax = leg.hasMax;
  v.best10sKn = leg.best10sKn;
  v.hasBest10s = leg.hasBest10s;
  return v;
}

}  // namespace stats_engine
