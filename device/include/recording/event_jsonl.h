#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "espnow_protocol.h"

// Pure formatting for the events sidecar (issue #139): one JSON object per
// line, appended next to the active track CSV and ingested by the server's
// track upload. No filesystem access here — mirrors GPSCSV::formatRow.
//
// Line shape (keep in step with server/moana/tracks/events_jsonl.py):
//   {"kind":"SAIL","utc":1753900000}\n
//
// The `kind` field carries the *mode name* for mode changes — the whole
// Mode.MODE_CHOICES vocabulary since issue #146, not just SAIL/MOTOR/OTHER.
// The wire kind collapsed to one EVENT_MODE_CHANGE at the same time, but the
// sidecar shape deliberately did not change: sidecars already written to SD
// stay parseable, and the server keeps one flat kind -> mode mapping.
namespace EventJsonl {

// The name written for an event: the mode's name for mode changes, otherwise
// the event category's own name.
inline const char* kindName(uint8_t kind, uint8_t mode) {
  if (kind == moana::EVENT_MODE_CHANGE) {
    // moana::sailingModeName is the single source of these strings, shared
    // with the watch UI; UNKNOWN for a mode this firmware doesn't know.
    return moana::sailingModeName(mode);
  }
  switch (kind) {
    case moana::EVENT_VOICE_MARKER:
      return "VOICE";
    default:
      return "UNKNOWN";
  }
}

// Formats one sidecar line (including the trailing newline) into buf.
// Returns the line length, or 0 if buf is too small.
inline size_t formatLine(char* buf, size_t bufSize, uint8_t kind, uint8_t mode,
                         uint32_t utcSeconds) {
  int len = snprintf(buf, bufSize, "{\"kind\":\"%s\",\"utc\":%lu}\n",
                     kindName(kind, mode), (unsigned long)utcSeconds);
  return (len > 0 && (size_t)len < bufSize) ? (size_t)len : 0;
}

}  // namespace EventJsonl
