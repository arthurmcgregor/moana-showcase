#pragma once

#include <Arduino.h>

// Pure filename validation and normalization — no filesystem access.
namespace TrackFilename {

inline bool isValid(const String& filename) {
  if (!filename.startsWith("/gps_") || !filename.endsWith(".csv")) {
    return false;
  }
  if (filename.indexOf("..") >= 0) {
    return false;
  }
  return true;
}

inline String normalize(const String& filename) {
  if (filename.startsWith("/")) {
    return filename;
  }
  return "/" + filename;
}

// Events sidecar (issue #139): one JSONL file of watch events alongside each
// track CSV, carried to SD by the same sync flow.

// "/gps_X.csv" -> "/gps_X.events.jsonl". Caller passes a valid track path.
inline String eventSidecarFor(const String& trackPath) {
  String base = trackPath;
  if (base.endsWith(".csv")) {
    base = base.substring(0, base.length() - 4);
  }
  return base + ".events.jsonl";
}

inline bool isEventSidecar(const String& filename) {
  if (!filename.startsWith("/gps_") || !filename.endsWith(".events.jsonl")) {
    return false;
  }
  if (filename.indexOf("..") >= 0) {
    return false;
  }
  return true;
}

}  // namespace TrackFilename
