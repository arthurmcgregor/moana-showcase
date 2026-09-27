// New Zealand local-time offset, daylight saving included (issue #175).
//
// The Speed tile's clock is the one place on either firmware that renders a
// local time — the device records UTC into the CSV and the sidecar, and the
// web app renders in the browser's zone. So the DST rule lives here, applied
// at render time to whatever UTC the timebase hands over.
//
// It was a fixed `12 * 3600` until this module: correct over NZST (April to
// September) and an hour behind for the whole sailing season.
//
// Pure logic — no Arduino, no LVGL, no radio; unit-tested in test_native.
#pragma once

#include <stdint.h>

namespace nz_time {

constexpr int32_t kNzstOffsetSeconds = 12 * 3600;
constexpr int32_t kNzdtOffsetSeconds = 13 * 3600;

// Seconds to add to UTC to get New Zealand local time at that instant.
// Anything before kPlausibleEpoch-era timestamps still answers — the rule is
// evaluated per instant, not per install.
int32_t utcOffsetSeconds(uint32_t utcSeconds);

// True when the instant falls inside New Zealand daylight time.
bool isDaylightTime(uint32_t utcSeconds);

}  // namespace nz_time
