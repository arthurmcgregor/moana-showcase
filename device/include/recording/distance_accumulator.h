#pragma once

// Cumulative session distance (issues #142/#65) — pure accumulation logic,
// no hardware, no TinyGPSPlus include. The great-circle distance function is
// injected: production wires TinyGPSPlus::distanceBetween (App), native
// tests inject a deterministic stub.
//
// Semantics (scoping decision on #142):
// - Integrates metres between consecutive valid fixes while recording is
//   ACTIVE only.
// - Resets to 0 when it first sees an ACTIVE fix after not-ACTIVE — the
//   IDLE→ACTIVE transition as observed at fix cadence (recording can only
//   start with GPS locked, so the first ACTIVE fix follows the transition
//   within ~1 s).
// - Holds its value after recording stops (the telemetry echo must keep
//   showing the finished session's total).
// - Fix dropouts never accumulate: onFixLost() drops the anchor, and the
//   next valid fix re-anchors WITHOUT adding the straight-line jump across
//   the gap.
//
// Owned by AppController (fed on the app-controller task); totalMeters() is
// atomic because composeTelemetry reads it from the ESP-NOW send task.

#include <atomic>
#include <cstdint>
#include <functional>

class DistanceAccumulator {
 public:
  // (lat1, lng1, lat2, lng2) -> metres. TinyGPSPlus::distanceBetween in
  // production.
  using DistanceFn = std::function<double(double, double, double, double)>;

  void init(DistanceFn distanceBetween);

  // One valid GPS fix, with whether recording is ACTIVE right now.
  void onFix(double lat, double lng, bool recordingActive);

  // The fix stream went invalid (GPS no longer LOCKED). No accumulation
  // across the gap — the next valid fix becomes a fresh anchor.
  void onFixLost();

  // Whole metres recorded so far this session (held after stop). Safe to
  // read from any task.
  uint32_t totalMeters() const { return publishedMeters_.load(); }

 private:
  DistanceFn distance_;
  // Fractional metres matter: at sailing speeds a 1 Hz fix advances ~0.5-5 m,
  // so accumulate in double and publish rounded whole metres.
  double totalMeters_ = 0.0;
  double anchorLat_ = 0.0;
  double anchorLng_ = 0.0;
  bool anchorValid_ = false;
  bool wasActive_ = false;
  std::atomic<uint32_t> publishedMeters_{0};
};
