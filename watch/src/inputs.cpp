#include "inputs.h"

#include <LilyGoLib.h>

namespace inputs {

constexpr int kBootButtonPin = 0;  // "custom button", LOW when pressed
constexpr uint32_t kLongPressMs = 600;
constexpr uint32_t kDoubleClickGapMs = 300;
constexpr uint32_t kDebounceMs = 30;

static Callbacks cb = {};

// PWR crown click (issue #142), ported from the watch-scoping-firmware
// branch: the AXP2101 raises a PKEY IRQ, LilyGoLib turns it into a
// POWER_EVENT dispatched from instance.loop() — this listener coexists with
// LilyGoLib's own PMU handling rather than polling the IRQ registers itself.
static void onPowerEvent(DeviceEvent_t event, void* params, void*) {
  if (event != POWER_EVENT) {
    return;
  }
  if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED &&
      cb.onPwrClick) {
    cb.onPwrClick();
  }
}

void begin(const Callbacks& callbacks) {
  cb = callbacks;
  pinMode(kBootButtonPin, INPUT_PULLUP);

  instance.pmu.clearIrqStatus();
  instance.pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                         XPOWERS_AXP2101_PKEY_LONG_IRQ);
  instance.onEvent(onPowerEvent, POWER_EVENT, nullptr);
}

void haptic(uint8_t effect) {
  instance.drv.setWaveform(0, effect);
  instance.drv.setWaveform(1, 0);  // end of sequence
  instance.drv.run();
}

void hapticSlots(const uint8_t* slots, size_t count) {
  if (count > kHapticSlots) {
    count = kHapticSlots;
  }
  for (size_t i = 0; i < count; i++) {
    instance.drv.setWaveform((uint8_t)i, slots[i]);
  }
  if (count < kHapticSlots) {
    instance.drv.setWaveform((uint8_t)count, 0);  // end of sequence
  }
  instance.drv.run();
}

// Single/double/long press classifier (ported from watch-scoping-firmware).
// A single press is only reported after the double-click gap expires, so the
// three gestures are unambiguous.
void poll() {
  static bool pressed = false;
  static uint32_t pressedAt = 0;
  static uint32_t releasedAt = 0;
  static bool longFired = false;
  static int pendingClicks = 0;

  uint32_t now = millis();
  bool level = digitalRead(kBootButtonPin) == LOW;

  if (level && !pressed && now - releasedAt > kDebounceMs) {
    pressed = true;
    pressedAt = now;
    longFired = false;
  } else if (!level && pressed && now - pressedAt > kDebounceMs) {
    pressed = false;
    releasedAt = now;
    if (!longFired) {
      pendingClicks++;
      if (pendingClicks == 2) {
        pendingClicks = 0;
        if (cb.onDoublePress) {
          cb.onDoublePress();
        }
      }
    }
  }

  if (pressed && !longFired && now - pressedAt >= kLongPressMs) {
    longFired = true;
    pendingClicks = 0;
    if (cb.onLongPress) {
      cb.onLongPress();
    }
  }

  if (!pressed && pendingClicks == 1 && now - releasedAt >= kDoubleClickGapMs) {
    pendingClicks = 0;
    if (cb.onSinglePress) {
      cb.onSinglePress();
    }
  }
}

}  // namespace inputs
