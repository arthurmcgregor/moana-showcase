#pragma once

struct TouchEvent {
  enum Type { NONE, CLICK, HOLD };
  Type type = NONE;
  int x = 0;
  int y = 0;
};

// Abstracts touch hardware (M5.Touch).
// In production, implemented by M5TouchInput.
// In tests, implemented by FakeTouchInput.
class TouchInput {
 public:
  virtual ~TouchInput() = default;

  // Call once per poll cycle. Updates internal state.
  virtual void update() = 0;

  // Returns the touch event detected this cycle (if any).
  virtual TouchEvent poll() = 0;
};
