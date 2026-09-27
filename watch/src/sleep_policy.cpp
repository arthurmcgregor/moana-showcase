#include "sleep_policy.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>

namespace sleep_policy {

// Screen blanks after this much LVGL input inactivity; any touch wakes it.
// The touch controller stays powered, so the first tap both wakes the panel
// and resets LVGL's inactivity clock.
constexpr uint32_t kScreenOffMs = 60000;

// How long after a wake a touch is still assumed to be the wake itself. The
// wake happens on the 250 ms tick, so a press that started while the panel was
// dark can be released — and reported as a click — after screenOff has already
// cleared; the grace has to outlast one tick. See touchIsWake().
constexpr uint32_t kWakeGraceMs = 500;

static bool screenOff = false;
static uint32_t wokeAtMs = 0;
static uint32_t sleptAtMs = 0;
static bool everWoke = false;

// --- wake-touch gate (issue #159) -------------------------------------------
//
// The touch controller stays powered through sleepDisplay() and LVGL has no
// idea the panel is dark, so a stroke meant only to wake also lands as a real
// press: a tap clicks whatever is underneath, a swipe pages the carousel.
// Guarding each control individually can't fix the swipe — by the time a
// screen sees a gesture the tileview has already claimed the pan as a scroll.
//
// So the stroke is swallowed one layer lower, in the input device's read
// callback, before LVGL's event machinery ever sees a press. The original
// callback still runs (it drives the hardware); we only rewrite what it
// reports upward.
static lv_indev_t* touchIndev = nullptr;
static lv_indev_read_cb_t chainedRead = nullptr;
static bool swallowing = false;

static void gatedRead(lv_indev_t* indev, lv_indev_data_t* data) {
  chainedRead(indev, data);

  // A press that begins while the panel is dark is a wake, whatever it lands
  // on — and stays a wake until the finger lifts, so the rest of the stroke
  // can't act either once tick() has lit the panel mid-swipe.
  if (screenOff && data->state == LV_INDEV_STATE_PRESSED) {
    swallowing = true;
  }
  if (!swallowing) {
    return;
  }
  if (data->state == LV_INDEV_STATE_RELEASED) {
    swallowing = false;  // finger up; the next touch is a real one
    return;              // LVGL never saw the press, so the release is a no-op
  }

  // Report "not touched", but register the touch as user activity by hand —
  // LVGL only resets its inactivity clock on a press it can see, and that
  // clock is what tick() wakes on.
  lv_display_trigger_activity(NULL);
  data->state = LV_INDEV_STATE_RELEASED;
}

void begin() {
  for (lv_indev_t* d = lv_indev_get_next(NULL); d != NULL;
       d = lv_indev_get_next(d)) {
    if (lv_indev_get_type(d) == LV_INDEV_TYPE_POINTER) {
      touchIndev = d;
      break;
    }
  }
  if (touchIndev == nullptr) {
    return;  // no pointer device (unexpected); leave input untouched
  }
  chainedRead = lv_indev_get_read_cb(touchIndev);
  if (chainedRead != nullptr) {
    lv_indev_set_read_cb(touchIndev, gatedRead);
  }
}

bool tick() {
  uint32_t idle = lv_display_get_inactive_time(NULL);
  if (!screenOff && idle >= kScreenOffMs) {
    screenOff = true;
    sleptAtMs = millis();
    instance.sleepDisplay();
  } else if (screenOff && idle < kScreenOffMs) {
    screenOff = false;
    wokeAtMs = millis();
    everWoke = true;
    instance.wakeupDisplay();
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    // A sleeping panel drops every write, but LVGL flushed them all the same
    // and thinks the glass shows what it drew — the sleep homing to Speed is
    // one such frame. Without a full redraw the wake shows the last frame
    // the panel took, patched only where a label has changed since.
    lv_obj_invalidate(lv_screen_active());
  }
  return screenOff;
}

bool touchIsWake(uint32_t nowMs) {
  // Dark panel: the touch can only be a wake, whatever it landed on. Just
  // woken: this is very likely the release of that same wake touch.
  return screenOff || (everWoke && nowMs - wokeAtMs < kWakeGraceMs);
}

uint32_t asleepForMs(uint32_t nowMs) {
  return screenOff ? nowMs - sleptAtMs : 0;
}

void noteActivity() {
  // Buttons don't route through LVGL input devices, so their presses are
  // invisible to lv_display_get_inactive_time(); reset the clock manually.
  // tick() then handles the actual wake, exactly as it does for touch.
  lv_display_trigger_activity(NULL);
}

}  // namespace sleep_policy
