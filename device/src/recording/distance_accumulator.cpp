#include "recording/distance_accumulator.h"

void DistanceAccumulator::init(DistanceFn distanceBetween) {
  distance_ = std::move(distanceBetween);
}

void DistanceAccumulator::onFix(double lat, double lng, bool recordingActive) {
  if (!recordingActive) {
    // Not recording: hold the published total (a finished session's distance
    // stays on the echo) and keep no anchor — a stale pre-start position
    // must never contribute to the next session's first segment.
    wasActive_ = false;
    anchorValid_ = false;
    return;
  }

  if (!wasActive_) {
    // Recording just started (as observed at fix cadence): fresh session.
    totalMeters_ = 0.0;
    anchorValid_ = false;
    wasActive_ = true;
    publishedMeters_.store(0);
  }

  if (anchorValid_ && distance_) {
    totalMeters_ += distance_(anchorLat_, anchorLng_, lat, lng);
    publishedMeters_.store(static_cast<uint32_t>(totalMeters_ + 0.5));
  }
  anchorLat_ = lat;
  anchorLng_ = lng;
  anchorValid_ = true;
}

void DistanceAccumulator::onFixLost() {
  // Gap in the valid-fix stream: never integrate across it. The total holds;
  // the next valid fix re-anchors without accumulating.
  anchorValid_ = false;
}
