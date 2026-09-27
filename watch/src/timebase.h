#pragma once

#include <stdint.h>

// UTC timebase for the clock and for event timestamps (issues #139/#144).
//
// A RAM reference (last known UTC + the millis() elapsed since) is the fast
// path: now() is read on every 250 ms tick and by every mode press, and the
// watch's I2C bus carries the PMU, touch controller and haptic driver with
// nothing arbitrating it — a hardware read per tick does not belong there.
//
// The hardware RTC (issue #144) is what makes that reference survive a reboot.
// It is read exactly once, at begin(), and written when telemetry shows the
// held time has drifted — or when the last write was refused, because a write
// this side cannot confirm is a clock that silently stops surviving reboots.
// Between those moments the RTC is not touched.
//
// now() returns 0 — the protocol's documented "time unknown" — until either
// the RTC hands over a plausible time or a telemetry packet carries one.
//
// Pure logic behind a port: no Arduino/LVGL/radio includes here, the PCF85063
// adapter lives in rtc_clock.cpp, and test_native uses a fake.
namespace timebase {

// Hardware clock port. Mirrors event_queue::Store — production adapter on the
// watch's RTC, in-memory fake in tests.
class Clock {
 public:
  virtual ~Clock() = default;

  // UTC the hardware is holding, or 0 when it is holding nothing real (never
  // disciplined, or its oscillator stopped since it last was).
  virtual uint32_t readUtc() = 0;

  // Discipline the hardware to utcSeconds. False if the write did not land —
  // and an adapter that cannot tell must go and look, because a false here is
  // the only thing that gets the write retried.
  virtual bool writeUtc(uint32_t utcSeconds) = 0;
};

// Below this, a time read back from hardware is not one this firmware wrote:
// a factory-fresh or half-reset RTC reads out somewhere around 2000-01-01 with
// its integrity bit clear, and rendering that is worse than rendering nothing.
// 2023-11-14, comfortably before the first watch ever ran this code.
constexpr uint32_t kPlausibleEpoch = 1700000000;

// How far the hardware may drift before it is rewritten. The RAM reference is
// rebased on every packet regardless — this threshold only spares the I2C bus
// a write per second for a clock that is already right. Two seconds because
// telemetry UTC and now() are both whole seconds, so a correct clock still
// disagrees by one about half the time.
constexpr uint32_t kDriftThresholdSeconds = 2;

// Adopt the hardware's time if it is holding a plausible one. Call once at
// startup, before the first now(). `clock` may be null (native tests that
// don't care about the hardware half).
void begin(Clock* clock, uint32_t nowMs);

// Record a UTC reference observed at refMs (millis() when the telemetry packet
// carrying it arrived), and discipline the hardware if it has drifted.
// utcSeconds == 0 (GPS time unknown) is ignored.
void feed(uint32_t utcSeconds, uint32_t refMs);

// Current UTC estimate in Unix seconds, or 0 if nothing has ever set one.
// Assumes nowMs and the last reference are within one millis() wrap (~49 days)
// of each other — guaranteed by the 1 Hz telemetry cadence in practice.
uint32_t now(uint32_t nowMs);

// Test hook: forget the reference and the clock port (fresh-boot state).
void reset();

}  // namespace timebase
