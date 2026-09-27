// Session/leg statistics engine (issue #142).
//
// Pure decision logic, same pattern as command_channel/event_queue: no
// Arduino, no LVGL, no radio includes — ui.cpp feeds it one Input per UI
// tick (with `packet` set when a fresh telemetry packet arrived) and screens
// read the views. Unit-tested in test/test_native/.
//
// Speed pipeline (binding, from the issue spec):
//   gate (fix_valid, hdop < 2.5, physically-impossible acceleration)
//     -> median-of-5 filter
//     -> per-leg AND per-session trackers: strict monotonic MAX of the
//        cleaned stream + best rolling 10 s mean over received samples.
// AVG is distance-derived (distance / elapsed), never a sample mean.
//
// Distance comes purely from the boat's cumulative echo (scoping decision):
//   leg distance     = cumulative - legStartCumulative
//   session distance = cumulative - sessionStartCumulative
// No watch-side haversine anywhere. If the echo ever drops below a stored
// baseline (the device accumulator restarted under us), the baseline is
// re-based instead of underflowing.
//
// Leg cutting is echo-driven: a new leg starts when the boat-echoed
// current_mode changes, so device- and watch-originated changes cut
// identically. A same-mode re-assert (PWR-click manual cut, or the future
// picker in #146) can't be seen as a mode change — the engine detects it as
// the ack frontier (last_event_seq_acked) advancing while the echoed mode
// stays the same: the boat applied event(s) that didn't move the echo.
// Several events applied between two packets with an unchanged net mode
// therefore collapse into ONE cut — honest, since the intermediate legs
// would carry no samples anyway.
//
// Cuts are debounced retroactively (issue #161): a cut arriving less than
// kMinLegMs after the current leg began relabels that leg instead of closing
// it, because a leg that short is a mis-press the user has just corrected.
// Nothing is delayed to achieve this — see cutLeg().
//
// Session lifecycle: reset when the echoed recording state transitions from
// not-recording to STARTING/ACTIVE; frozen (not cleared) when it stops. If
// the very first packet the watch ever hears already shows recording in
// progress (watch rebooted mid-sail), the session is adopted with a zero
// distance baseline — the cumulative echo restores the session distance
// instantly; elapsed can only be counted from now.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "espnow_protocol.h"

namespace stats_engine {

// --- sample gate ---
constexpr float kHdopMax = 2.5f;
// A yacht can't gain more than ~2 kn between 1 Hz samples. The allowed
// delta scales with the time since the last ACCEPTED sample (clamped below)
// so a genuine sustained change — or a long telemetry gap — re-syncs the
// gate instead of locking it out forever.
constexpr float kAccelKnPerSec = 2.0f;
constexpr float kAccelFloorKn = 2.0f;
constexpr float kAccelCeilKn = 20.0f;

// --- median filter ---
constexpr size_t kMedianTaps = 5;
// Cleaned values only start flowing once this many accepted samples exist,
// so a spike among the very first samples can't poison the monotonic MAX.
constexpr size_t kMedianMinTaps = 3;

// --- best rolling 10 s window ---
constexpr uint32_t kWindowMs = 10000;
// A window is only a candidate when its samples span at least this much
// real time (i.e. never inside the first ~10 s of a leg/session) ...
constexpr uint32_t kWindowMinSpanMs = 9000;
// ... and no two consecutive samples are further apart than this — a window
// across a telemetry gap yields no candidate rather than a synthetic one.
constexpr uint32_t kWindowMaxGapMs = 2000;
constexpr size_t kWindowCap = 24;  // 1 Hz nominal cadence, 2x headroom

// Bounded leg history: oldest legs fall off, numbering keeps counting.
constexpr size_t kMaxLegs = 32;

// Mis-press debounce (issue #161): a cut this soon after the current leg began
// is a correction, so the leg is relabelled rather than cut — see cutLeg().
// 20 s to the second matches MODE_DEBOUNCE_SECONDS in
// server/moana/tracks/events_jsonl.py, which collapses the same burst on
// import. The two must agree: a mis-press has to stop drawing a five-second
// leg on the wrist and a five-second segment on the web map together.
constexpr uint32_t kMinLegMs = 20000;

constexpr float kMpsToKnots = 1.943844f;

struct LegView {
  uint16_t number = 0;  // 1-based within the session; survives ring eviction
  uint8_t mode = moana::MODE_UNKNOWN;  // moana::SailingMode of this leg
  uint32_t elapsedMs = 0;
  uint32_t distanceM = 0;
  float avgKn = 0;  // distance-derived
  float maxKn = 0;
  bool hasMax = false;
  float best10sKn = 0;
  bool hasBest10s = false;
};

struct SessionView {
  bool exists = false;  // a session has been observed since boot
  bool live = false;    // accumulating (echo says recording in progress)
  uint32_t elapsedMs = 0;
  uint32_t distanceM = 0;
  float avgKn = 0;
  float maxKn = 0;
  bool hasMax = false;
  float best10sKn = 0;
  bool hasBest10s = false;
};

class Engine {
 public:
  struct Input {
    uint32_t nowMs = 0;
    // True when the fields below carry a telemetry packet not seen before.
    // With packet == false only nowMs is read (elapsed keeps counting).
    bool packet = false;
    float sogKnots = 0;
    bool fixValid = false;
    float hdop = 0;
    uint32_t cumulativeDistanceM = 0;
    uint8_t recordingState = moana::RECORDING_IDLE;  // moana::RecordingStatus
    uint8_t currentMode = moana::MODE_UNKNOWN;       // moana::SailingMode
    uint16_t eventSeqAcked = moana::kEventSeqNone;
  };

  // Call once per UI tick.
  void tick(const Input& in);

  // Manual hold-to-reset: restart the session (and legs) in place from the
  // last packet's snapshot. Only while live — returns false otherwise
  // (caller gives the error haptic).
  bool resetSession();

  bool sessionLive() const { return live_; }
  SessionView session() const;

  // Legs retained (<= kMaxLegs). leg(0) = oldest retained,
  // leg(legCount()-1) = current leg.
  size_t legCount() const { return legCount_; }
  LegView leg(size_t idx) const;

 private:
  struct Leg {
    uint16_t number = 0;
    uint8_t mode = moana::MODE_UNKNOWN;
    uint32_t startMs = 0;
    uint32_t startCumM = 0;
    // Set when the leg closes (cut or session freeze).
    uint32_t frozenElapsedMs = 0;
    uint32_t frozenDistanceM = 0;
    bool closed = false;
    float maxKn = 0;
    bool hasMax = false;
    float best10sKn = 0;
    bool hasBest10s = false;
  };

  Leg& currentLeg() { return legs_[(legHead_ + legCount_ - 1) % kMaxLegs]; }
  const Leg& legAt(size_t idx) const {
    return legs_[(legHead_ + idx) % kMaxLegs];
  }

  void startSession(const Input& in, uint32_t baselineCumM);
  void freezeSession(const Input& in);
  void pushLeg(uint8_t mode, uint32_t startMs, uint32_t startCumM);
  void cutLeg(uint8_t newMode, const Input& in);
  void resetPipeline();
  void ingestSample(const Input& in);
  void ingestCleaned(float kn, uint32_t nowMs);
  float medianOfBuffer() const;

  static LegView viewOf(const Leg& leg, uint32_t nowMs, uint32_t lastCumM,
                        bool live);

  // Session state.
  bool live_ = false;
  bool sessionExists_ = false;
  uint32_t sessionStartMs_ = 0;
  uint32_t sessionStartCumM_ = 0;
  uint32_t sessionFrozenElapsedMs_ = 0;
  uint32_t sessionFrozenDistanceM_ = 0;
  float sessionMaxKn_ = 0;
  bool sessionHasMax_ = false;
  float sessionBest10sKn_ = 0;
  bool sessionHasBest10s_ = false;

  // Leg ring.
  Leg legs_[kMaxLegs];
  size_t legHead_ = 0;
  size_t legCount_ = 0;
  uint16_t totalLegs_ = 0;

  // Echo tracking.
  bool packetSeen_ = false;
  bool wasRecording_ = false;
  uint8_t lastMode_ = moana::MODE_UNKNOWN;
  uint16_t lastAck_ = moana::kEventSeqNone;
  uint32_t lastCumM_ = 0;
  uint32_t nowMs_ = 0;

  // Gate state.
  bool haveAccepted_ = false;
  float lastAcceptedKn_ = 0;
  uint32_t lastAcceptMs_ = 0;

  // Median-of-5 ring over accepted samples.
  float median_[kMedianTaps] = {};
  size_t medianNext_ = 0;
  size_t medianFill_ = 0;

  // Rolling 10 s window of cleaned samples (timestamped).
  struct WindowSample {
    uint32_t tMs;
    float kn;
  };
  WindowSample window_[kWindowCap];
  size_t windowHead_ = 0;
  size_t windowCount_ = 0;
};

}  // namespace stats_engine
