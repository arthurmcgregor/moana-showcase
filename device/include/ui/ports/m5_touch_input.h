#pragma once

#include "ui/ports/touch_input.h"

// Production TouchInput backed by M5Unified touch hardware.
class M5TouchInput : public TouchInput {
 public:
  void update() override;
  TouchEvent poll() override;
};
