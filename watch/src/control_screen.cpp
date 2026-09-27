#include "control_screen.h"

#include <stdio.h>

#include "fonts.h"

namespace control_screen {

// Palette (docs/watch-screen-mockups.html).
constexpr uint32_t kColDim = 0xAAB1BA;
constexpr uint32_t kColFaint = 0x59606A;
constexpr uint32_t kColRec = 0xFF4D4F;
constexpr uint32_t kColOk = 0x34D178;
constexpr uint32_t kColAmber = 0xFFB648;
constexpr uint32_t kColStartBg = 0x0D3524;
constexpr uint32_t kColStopBg = 0x3A1113;
constexpr uint32_t kColDisabledBg = 0x17191C;

constexpr int kBtnWidth = 290;
constexpr int kBtnHeight = 92;
constexpr uint32_t kHoldMs = 1000;

// What the hold button currently offers. Derived from link + command
// pending + the boat's echoed state — never from a local assumption.
enum class Mode {
  START,    // echo idle: green HOLD TO START
  STOP,     // echo active: red HOLD TO STOP
  RESET,    // echo failed: red HOLD TO RESET (set-off resets FAILED -> IDLE)
  SENT,     // our command pending: disabled, resend loop running
  WAIT,     // boat transitioning on its own (device-side start/stop)
  NO_LINK,  // link lost: boat state unknown, control disabled
};

static lv_obj_t* stateLabel;
static lv_obj_t* holdBtn;
static lv_obj_t* fillBar;
static lv_obj_t* btnLabel;
static lv_obj_t* metaLabel;

static HoldConfirmedFn confirmCb = nullptr;
static Mode mode = Mode::NO_LINK;
static bool holding = false;

static bool actionable(Mode m) {
  return m == Mode::START || m == Mode::STOP || m == Mode::RESET;
}

// The absolute state a completed hold requests: start -> on; stop and
// reset -> off (a set-off normalises FAILED back to IDLE on the boat).
static bool desiredOnFor(Mode m) {
  return m == Mode::START;
}

static const char* buttonText(Mode m) {
  switch (m) {
    case Mode::START:
      return "HOLD TO START";
    case Mode::STOP:
      return "HOLD TO STOP";
    case Mode::RESET:
      return "HOLD TO RESET";
    case Mode::SENT:
      return "SENT";
    case Mode::WAIT:
      return "WAIT";
    case Mode::NO_LINK:
    default:
      return "- - -";
  }
}

static void applyButtonStyle(Mode m) {
  uint32_t bg, accent, fill;
  switch (m) {
    case Mode::START:
      bg = kColStartBg;
      accent = kColOk;
      fill = kColOk;
      break;
    case Mode::STOP:
    case Mode::RESET:
      bg = kColStopBg;
      accent = kColRec;
      fill = kColRec;
      break;
    case Mode::SENT:
    case Mode::WAIT:
    case Mode::NO_LINK:
    default:
      bg = kColDisabledBg;
      accent = kColFaint;
      fill = kColFaint;
      break;
  }
  lv_obj_set_style_bg_color(holdBtn, lv_color_hex(bg), 0);
  lv_obj_set_style_border_color(holdBtn, lv_color_hex(accent), 0);
  lv_obj_set_style_text_color(btnLabel, lv_color_hex(accent), 0);
  lv_obj_set_style_bg_color(fillBar, lv_color_hex(fill), 0);
  lv_label_set_text(btnLabel, buttonText(m));
}

static void fillAnimExec(void* var, int32_t v) {
  lv_obj_set_width(static_cast<lv_obj_t*>(var), v);
}

static void cancelHold() {
  if (!holding) {
    return;
  }
  holding = false;
  lv_anim_delete(fillBar, fillAnimExec);
  lv_obj_set_width(fillBar, 0);
  lv_label_set_text(btnLabel, buttonText(mode));
}

static void holdComplete(lv_anim_t*) {
  holding = false;
  lv_obj_set_width(fillBar, 0);
  bool desiredOn = desiredOnFor(mode);
  lv_label_set_text(btnLabel, buttonText(mode));
  if (confirmCb) {
    confirmCb(desiredOn);
  }
}

static void startHold() {
  holding = true;
  lv_label_set_text(btnLabel, "KEEP HOLDING...");
  lv_obj_set_width(fillBar, 0);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, fillBar);
  lv_anim_set_exec_cb(&a, fillAnimExec);
  lv_anim_set_values(&a, 0, kBtnWidth);
  lv_anim_set_duration(&a, kHoldMs);
  lv_anim_set_completed_cb(&a, holdComplete);
  lv_anim_start(&a);
}

static void onButtonEvent(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    // Presses only arm the sweep when the button offers an action; SENT /
    // WAIT / NO_LINK stay inert.
    if (actionable(mode) && !holding) {
      startHold();
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    // Release before the 1 s sweep completes = nothing sent.
    cancelHold();
  }
}

static Mode computeMode(const model::AppModel& m) {
  if (m.link == model::LinkState::LOST) {
    return Mode::NO_LINK;
  }
  if (m.commandPending) {
    return Mode::SENT;
  }
  switch (m.recordingEcho) {
    case moana::RECORDING_IDLE:
      return Mode::START;
    case moana::RECORDING_ACTIVE:
      return Mode::STOP;
    case moana::RECORDING_FAILED:
      return Mode::RESET;
    case moana::RECORDING_STARTING:
    case moana::RECORDING_STOPPING:
    default:
      return Mode::WAIT;
  }
}

static void refreshStateLabel(const model::AppModel& m) {
  const char* text;
  uint32_t color;

  if (m.commandTimedOut) {
    // Brief error state after the 10 s command timeout; the label returns
    // to the echoed truth when ui.cpp drops the flag.
    text = "NO REPLY";
    color = kColRec;
  } else if (m.link == model::LinkState::LOST) {
    text = "NO LINK";
    color = kColDim;
  } else if (m.commandPending) {
    text = m.commandDesiredOn ? "STARTING..." : "STOPPING...";
    color = kColAmber;
  } else {
    switch (m.recordingEcho) {
      case moana::RECORDING_ACTIVE:
        text = "RECORDING";
        color = kColRec;
        break;
      case moana::RECORDING_STARTING:
        text = "STARTING...";
        color = kColAmber;
        break;
      case moana::RECORDING_STOPPING:
        text = "STOPPING...";
        color = kColAmber;
        break;
      case moana::RECORDING_FAILED:
        text = "FAILED";
        color = kColAmber;
        break;
      case moana::RECORDING_IDLE:
      default:
        text = "IDLE";
        color = kColDim;
        break;
    }
  }

  lv_label_set_text(stateLabel, text);
  lv_obj_set_style_text_color(stateLabel, lv_color_hex(color), 0);
}

static void formatAge(char* buf, size_t len, const model::AppModel& m) {
  if (!m.haveTelemetry) {
    snprintf(buf, len, "no packets yet");
    return;
  }
  // Whole-second steps only: sub-second decimals flicker at the 250ms
  // refresh rate and carry no information at a 1Hz packet cadence.
  uint32_t age = m.linkAgeMs;
  if (age < 1000) {
    snprintf(buf, len, "last packet < 1s ago");
  } else if (age < 60000) {
    snprintf(buf, len, "last packet %lus ago", (unsigned long)(age / 1000));
  } else {
    snprintf(buf, len, "last packet %lum ago", (unsigned long)(age / 60000));
  }
}

void create(lv_obj_t* tile, HoldConfirmedFn onHoldConfirmed) {
  confirmCb = onHoldConfirmed;

  stateLabel = lv_label_create(tile);
  lv_obj_set_style_text_font(stateLabel, &font_ui_40, 0);
  lv_obj_set_style_text_color(stateLabel, lv_color_hex(kColDim), 0);
  lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, -115);
  lv_label_set_text(stateLabel, "NO LINK");

  holdBtn = lv_button_create(tile);
  lv_obj_set_size(holdBtn, kBtnWidth, kBtnHeight);
  lv_obj_align(holdBtn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(holdBtn, kBtnHeight / 2, 0);
  lv_obj_set_style_border_width(holdBtn, 3, 0);
  lv_obj_set_style_shadow_width(holdBtn, 0, 0);
  lv_obj_set_style_pad_all(holdBtn, 0, 0);
  lv_obj_add_event_cb(holdBtn, onButtonEvent, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(holdBtn, onButtonEvent, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(holdBtn, onButtonEvent, LV_EVENT_PRESS_LOST, nullptr);

  // Fill sweep: grows left -> right across the button over the 1 s hold.
  fillBar = lv_obj_create(holdBtn);
  lv_obj_remove_flag(fillBar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(fillBar, 0, kBtnHeight);
  lv_obj_align(fillBar, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(fillBar, kBtnHeight / 2, 0);
  lv_obj_set_style_border_width(fillBar, 0, 0);
  lv_obj_set_style_bg_opa(fillBar, LV_OPA_30, 0);

  btnLabel = lv_label_create(holdBtn);
  lv_obj_set_style_text_font(btnLabel, &font_ui_28, 0);
  lv_obj_center(btnLabel);

  metaLabel = lv_label_create(tile);
  lv_obj_set_style_text_font(metaLabel, &font_ui_24, 0);
  lv_obj_set_style_text_color(metaLabel, lv_color_hex(kColDim), 0);
  lv_obj_set_style_text_align(metaLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(metaLabel, LV_ALIGN_CENTER, 0, 120);
  lv_label_set_text(metaLabel, "");

  applyButtonStyle(mode);
}

void refresh(const model::AppModel& m) {
  Mode newMode = computeMode(m);
  if (newMode != mode) {
    // The world changed under an in-flight hold (echo flipped, link dropped,
    // command confirmed) — the sweep no longer means what the user started;
    // cancel rather than fire a stale action.
    cancelHold();
    mode = newMode;
    applyButtonStyle(mode);
  }

  refreshStateLabel(m);

  char age[48];
  formatAge(age, sizeof(age), m);
  lv_label_set_text_fmt(metaLabel, "%s\nqueue %d pending", age,
                        m.queuePending);
}

}  // namespace control_screen
