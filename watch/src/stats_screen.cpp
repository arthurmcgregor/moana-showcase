#include "stats_screen.h"

#include <stdio.h>

#include "fonts.h"

namespace stats_screen {

// Palette (docs/watch-screen-mockups.html).
constexpr uint32_t kColDim = 0xAAB1BA;
constexpr uint32_t kColFaint = 0x59606A;
constexpr uint32_t kColAmber = 0xFFB648;
constexpr uint32_t kColStale = 0x666666;

// One fixed leg card — it NEVER moves or resizes; a vertical swipe swaps
// which leg it shows (down = older, matching the mockup's "previous leg
// peeks above"). This replaces the earlier scroll panel, whose flex rebuild
// shifted the live numbers every time a leg was cut.
// 330 wide (matching the session line) so a nine-letter mode name and an
// h:mm:ss leg time still fit on the header row side by side (issue #146).
constexpr int kCardWidth = 330;
// Clears the header's icon row, which ends around y=42; the clock line that
// used to force this down to 100 moved to the Speed tile.
//
// The 40 px that freed up went into the grid rows rather than into the gap
// above the pinned session line: the card is bottom-limited by that line, not
// by the header, so sliding it up whole would only have opened a void in the
// middle of the tile. Taller rows instead — font_ui_40 values in a 47 px row
// were tight — and the card still ends at the same y=388.
constexpr int kCardTop = 60;
constexpr int kGridRowH = 57;
// Two header lines — "LEG n" on its own line, mode + leg time on the next —
// so MOTOR and h:mm:ss leg times can't collide.
constexpr int kHeaderRow2Y = 40;
constexpr int kGridTop = 100;
constexpr int kCardHeight = kGridTop + 4 * kGridRowH;

constexpr int kSessionLineOffsetY = -20;
constexpr int kSessionWidth = 330;
constexpr int kSessionHeight = 56;
constexpr uint32_t kHoldMs = 1000;

static lv_obj_t* card;
static lv_obj_t* legNumLabel;
static lv_obj_t* legModeLabel;
static lv_obj_t* legTimeLabel;
static lv_obj_t* gridValues[4];  // DIST / AVG / MAX / 10s
static lv_obj_t* sessionBtn;
static lv_obj_t* sessionFill;
static lv_obj_t* sessionLabel;

static ResetHoldFn resetCb = nullptr;
static bool holding = false;

// Leg being browsed, by its (eviction-stable) number. 0 = follow the
// current leg, including across cuts; a past leg stays pinned while new
// legs are cut. Resolved against the retained ring each refresh — an
// evicted or reset-away leg falls back to follow mode.
static uint16_t viewedLegNumber = 0;

// Set by refresh() so the gesture handler can step relative to what is
// actually on screen without re-deriving engine state.
static const stats_engine::Engine* lastEngine = nullptr;

// Shared protocol vocabulary (issue #146) — same names the chip and picker
// show; "- - -" for a leg whose mode was never classified.
static const char* modeText(uint8_t mode) {
  if (!moana::isSelectableSailingMode(mode)) {
    return "- - -";
  }
  return moana::sailingModeName(mode);
}

// mm:ss below an hour, h:mm:ss beyond — leg times are usually minutes,
// session times can run all day.
static void fmtDuration(char* buf, size_t len, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 3600) {
    snprintf(buf, len, "%lu:%02lu", (unsigned long)(s / 60),
             (unsigned long)(s % 60));
  } else {
    snprintf(buf, len, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
             (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  }
}

static void fmtKnots(char* buf, size_t len, float kn, bool has) {
  if (has) {
    snprintf(buf, len, "%.1f", (double)kn);
  } else {
    snprintf(buf, len, "--");
  }
}

// Index into the retained ring of the leg currently viewed; falls back to
// the current leg (and drops the pin) when the pinned number is gone.
static size_t viewedIndex(const stats_engine::Engine& eng) {
  size_t count = eng.legCount();
  if (viewedLegNumber != 0) {
    for (size_t i = 0; i < count; i++) {
      if (eng.leg(i).number == viewedLegNumber) {
        return i;
      }
    }
    viewedLegNumber = 0;  // evicted or session reset — follow current again
  }
  return count - 1;
}

void onShown() {
  // Follow mode again — the pin is a browse, not a setting, and the leg it
  // named may be many cuts behind by the time the tile is opened next.
  viewedLegNumber = 0;
}

// Vertical swipe pages through legs: down reveals the older leg "above",
// up moves back toward the current leg. Only TOP/BOTTOM are handled —
// LEFT/RIGHT belong to the tileview carousel (same split as speed_screen).
static void onGesture(lv_event_t* e) {
  (void)e;
  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr || lastEngine == nullptr ||
      lastEngine->legCount() == 0) {
    return;
  }
  const stats_engine::Engine& eng = *lastEngine;
  size_t idx = viewedIndex(eng);

  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_BOTTOM && idx > 0) {
    viewedLegNumber = eng.leg(idx - 1).number;
  } else if (dir == LV_DIR_TOP && idx + 1 < eng.legCount()) {
    size_t next = idx + 1;
    // Reaching the newest leg returns to follow mode, so the view keeps
    // tracking new cuts again.
    viewedLegNumber =
        next == eng.legCount() - 1 ? 0 : eng.leg(next).number;
  }
}

// --- hold-to-reset on the session line (control_screen's hold pattern) ---

static void fillAnimExec(void* var, int32_t v) {
  lv_obj_set_width(static_cast<lv_obj_t*>(var), v);
}

static void cancelHold() {
  if (!holding) {
    return;
  }
  holding = false;
  lv_anim_delete(sessionFill, fillAnimExec);
  lv_obj_set_width(sessionFill, 0);
}

static void holdComplete(lv_anim_t*) {
  holding = false;
  lv_obj_set_width(sessionFill, 0);
  if (resetCb) {
    resetCb();
  }
}

static void onSessionEvent(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (holding) {
      return;
    }
    holding = true;
    lv_label_set_text(sessionLabel, "HOLD TO RESET");
    lv_obj_set_width(sessionFill, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sessionFill);
    lv_anim_set_exec_cb(&a, fillAnimExec);
    lv_anim_set_values(&a, 0, kSessionWidth);
    lv_anim_set_duration(&a, kHoldMs);
    lv_anim_set_completed_cb(&a, holdComplete);
    lv_anim_start(&a);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    cancelHold();
  }
}

// --- construction ---

void create(lv_obj_t* tile, ResetHoldFn onResetHold) {
  resetCb = onResetHold;

  // The one leg card, at a fixed position: centre-anchored in the middle
  // band, clear of the corner arcs (watch/CLAUDE.md).
  card = lv_obj_create(tile);
  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, kCardWidth, kCardHeight);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kCardTop);

  legNumLabel = lv_label_create(card);
  lv_obj_set_style_text_font(legNumLabel, &font_ui_28, 0);
  lv_obj_set_style_text_color(legNumLabel, lv_color_hex(kColAmber), 0);
  lv_obj_set_style_text_letter_space(legNumLabel, 2, 0);
  lv_obj_align(legNumLabel, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_label_set_text(legNumLabel, "LEG -");

  legModeLabel = lv_label_create(card);
  lv_obj_set_style_text_font(legModeLabel, &font_ui_24, 0);
  lv_obj_set_style_text_color(legModeLabel, lv_color_hex(kColDim), 0);
  lv_obj_set_style_text_letter_space(legModeLabel, 1, 0);
  lv_obj_align(legModeLabel, LV_ALIGN_TOP_LEFT, 0, kHeaderRow2Y + 11);
  lv_label_set_text(legModeLabel, "- - -");

  legTimeLabel = lv_label_create(card);
  lv_obj_set_style_text_font(legTimeLabel, &font_ui_40, 0);
  lv_obj_set_style_text_color(legTimeLabel, lv_color_white(), 0);
  lv_obj_align(legTimeLabel, LV_ALIGN_TOP_RIGHT, 0, kHeaderRow2Y);
  lv_label_set_text(legTimeLabel, "--:--");

  static const char* kKeys[4] = {"DIST", "AVG", "MAX", "10s"};
  for (int i = 0; i < 4; i++) {
    int y = kGridTop + i * kGridRowH;
    lv_obj_t* k = lv_label_create(card);
    lv_obj_set_style_text_font(k, &font_ui_24, 0);
    lv_obj_set_style_text_color(k, lv_color_hex(kColDim), 0);
    lv_obj_set_style_text_letter_space(k, 2, 0);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, y + 10);
    lv_label_set_text(k, kKeys[i]);

    gridValues[i] = lv_label_create(card);
    lv_obj_set_style_text_font(gridValues[i], &font_ui_40, 0);
    lv_obj_set_style_text_color(gridValues[i], lv_color_white(), 0);
    lv_obj_align(gridValues[i], LV_ALIGN_TOP_RIGHT, 0, y);
    lv_label_set_text(gridValues[i], "--");
  }

  // Vertical swipe anywhere on the tile pages through legs. Same wiring as
  // speed_screen: the tile drops GESTURE_BUBBLE to become the gesture
  // target; horizontal pans claim the tileview as scroll_obj first, which
  // suppresses gestures — the carousel swipe can't fight this.
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(tile, onGesture, LV_EVENT_GESTURE, nullptr);

  // Pinned session line. Also the hold-to-reset target (mockup: hold on the
  // session line).
  sessionBtn = lv_button_create(tile);
  lv_obj_set_size(sessionBtn, kSessionWidth, kSessionHeight);
  lv_obj_align(sessionBtn, LV_ALIGN_BOTTOM_MID, 0, kSessionLineOffsetY);
  lv_obj_set_style_bg_opa(sessionBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(sessionBtn, 0, 0);
  lv_obj_set_style_pad_all(sessionBtn, 0, 0);
  lv_obj_add_event_cb(sessionBtn, onSessionEvent, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(sessionBtn, onSessionEvent, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(sessionBtn, onSessionEvent, LV_EVENT_PRESS_LOST,
                      nullptr);

  sessionFill = lv_obj_create(sessionBtn);
  lv_obj_remove_flag(sessionFill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(sessionFill, 0, kSessionHeight);
  lv_obj_align(sessionFill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(sessionFill, kSessionHeight / 2, 0);
  lv_obj_set_style_border_width(sessionFill, 0, 0);
  lv_obj_set_style_bg_color(sessionFill, lv_color_hex(kColAmber), 0);
  lv_obj_set_style_bg_opa(sessionFill, LV_OPA_30, 0);

  sessionLabel = lv_label_create(sessionBtn);
  lv_obj_set_style_text_font(sessionLabel, &font_ui_24, 0);
  lv_obj_set_style_text_color(sessionLabel, lv_color_hex(kColFaint), 0);
  lv_obj_center(sessionLabel);
  lv_label_set_text(sessionLabel, "no session");
}

void refresh(const model::AppModel& m) {
  const stats_engine::Engine& eng = *m.stats;
  lastEngine = m.stats;
  stats_engine::SessionView sv = eng.session();
  size_t count = eng.legCount();

  if (!sv.exists || count == 0) {
    viewedLegNumber = 0;
    lv_label_set_text(legNumLabel, "LEG -");
    lv_label_set_text(legModeLabel, "- - -");
    lv_label_set_text(legTimeLabel, "--:--");
    lv_obj_set_style_text_color(legTimeLabel, lv_color_hex(kColStale), 0);
    for (int i = 0; i < 4; i++) {
      lv_label_set_text(gridValues[i], "--");
      lv_obj_set_style_text_color(gridValues[i], lv_color_hex(kColStale), 0);
    }
    if (!holding) {
      lv_label_set_text(sessionLabel, "no session");
    }
    return;
  }

  size_t idx = viewedIndex(eng);
  bool isCurrent = idx == count - 1;
  stats_engine::LegView leg = eng.leg(idx);

  // Current leg: live white values, greyed on a stale link exactly like the
  // speed digits. Past legs: frozen values, always dim.
  bool stale = !m.haveTelemetry || m.link == model::LinkState::LOST;
  lv_color_t valueColor = !isCurrent ? lv_color_hex(kColDim)
                          : stale    ? lv_color_hex(kColStale)
                                     : lv_color_white();
  lv_obj_set_style_text_color(legTimeLabel, valueColor, 0);
  for (int i = 0; i < 4; i++) {
    lv_obj_set_style_text_color(gridValues[i], valueColor, 0);
  }

  lv_label_set_text_fmt(legNumLabel, "LEG %u", (unsigned)leg.number);
  lv_label_set_text(legModeLabel, modeText(leg.mode));
  char dur[16];
  fmtDuration(dur, sizeof(dur), leg.elapsedMs);
  lv_label_set_text(legTimeLabel, dur);

  lv_label_set_text_fmt(gridValues[0], "%.1fnm",
                        (double)leg.distanceM / 1852.0);
  lv_label_set_text_fmt(gridValues[1], "%.1f", (double)leg.avgKn);
  char buf[12];
  fmtKnots(buf, sizeof(buf), leg.maxKn, leg.hasMax);
  lv_label_set_text(gridValues[2], buf);
  fmtKnots(buf, sizeof(buf), leg.best10sKn, leg.hasBest10s);
  lv_label_set_text(gridValues[3], buf);

  // Session line stays put while a hold sweep is showing its own text.
  if (!holding) {
    char sdur[16];
    fmtDuration(sdur, sizeof(sdur), sv.elapsedMs);
    // No "ended" marker — a session time that stopped counting says it.
    lv_label_set_text_fmt(sessionLabel, "session %.1fnm \xE2\x80\xA2 %s",
                          (double)sv.distanceM / 1852.0, sdur);
  }
}

}  // namespace stats_screen
