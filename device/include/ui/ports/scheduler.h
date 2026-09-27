#pragma once

#include <cstdint>

// Abstracts timing (millis). In production, uses Arduino millis().
// In tests, implemented by FakeScheduler with controllable time.
class Scheduler {
 public:
  virtual ~Scheduler() = default;

  // Current time in milliseconds (monotonic).
  virtual uint32_t millis() = 0;
};
