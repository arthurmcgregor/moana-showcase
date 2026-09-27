#pragma once

#include "ui/ports/touch_input.h"

class FakeTouchInput : public TouchInput {
 public:
  void update() override { updateCount++; }

  TouchEvent poll() override {
    TouchEvent event = nextEvent;
    nextEvent = {};  // consume — returns NONE next time
    return event;
  }

  void injectTouch(TouchEvent event) {
    nextEvent = event;
  }

  void injectClick(int x, int y) {
    nextEvent = {TouchEvent::CLICK, x, y};
  }

  void injectHold(int x, int y) {
    nextEvent = {TouchEvent::HOLD, x, y};
  }

  // Test inspection
  int updateCount = 0;

 private:
  TouchEvent nextEvent{};
};
