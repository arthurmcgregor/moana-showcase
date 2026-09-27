#include "ui/ports/m5_touch_input.h"

#include <M5Unified.h>

void M5TouchInput::update() {
  M5.update();
}

TouchEvent M5TouchInput::poll() {
  TouchEvent event;
  if (M5.Touch.getCount() > 0) {
    const auto& detail = M5.Touch.getDetail(0);
    if (detail.wasHold()) {
      event.type = TouchEvent::HOLD;
      event.x = detail.x;
      event.y = detail.y;
    } else if (detail.wasClicked()) {
      event.type = TouchEvent::CLICK;
      event.x = detail.x;
      event.y = detail.y;
    }
  }
  return event;
}
