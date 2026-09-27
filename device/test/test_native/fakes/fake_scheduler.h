#pragma once

#include "ui/ports/scheduler.h"

class FakeScheduler : public Scheduler {
 public:
  uint32_t millis() override { return currentTime; }

  void setTime(uint32_t ms) { currentTime = ms; }
  void advanceTime(uint32_t ms) { currentTime += ms; }

  uint32_t currentTime = 0;
};
