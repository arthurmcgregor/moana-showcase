#pragma once

#include <Arduino.h>

#include "ui/ports/scheduler.h"

// Production Scheduler backed by Arduino millis().
class ArduinoScheduler : public Scheduler {
 public:
  uint32_t millis() override { return ::millis(); }
};
