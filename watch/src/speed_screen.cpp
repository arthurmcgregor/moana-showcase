#include "speed_screen.h"

#include "fonts.h"
#include "nz_time.h"

namespace speed_screen {

// Palette (docs/watch-screen-mockups.html).
constexpr uint32_t kColAmber = 0xFFB648;
constexpr uint32_t kColDim = 0xAAB1BA;
constexpr uint32_t kColFaint = 0x59606A;
constexpr uint32_t kColStale = 0x666666;

// The clock lives on this tile rather than in the floating header: it is a
// glance-at-the-tiller readout like the speed itself, and the other three
// tiles all want their top band for their own content. Anchored to the top so
// it reads as the header's second line — but well below the icon row (which
// ends around y=42), where it used to sit only 6 px clear and looked crammed
// against it. font_time_56 is 39 px tall, so this ends around y=103, still
// ~100 px above the SOG digits.
constexpr int kClockTop = 64;

// Vertical nudge keeps the digits centred in the full-width middle band,
// clear of the glass corner arcs (kCornerRadiusPx / 4 — see watch/CLAUDE.md).
constexpr int kSpeedOffsetY = 70 / 4;

// Speed-foot band per the mockups: mode chip plus one secondary value,
// bottom-centre keeps the row clear of the glass corner arcs. Stacked rather
// than side by side since issue #159 — a nine-letter mode name next to a
// secondary value ran the two together, and the mode is the more important
// of the pair, so it takes the line nearer the digits.
constexpr int kFootOffsetY = -34;

static lv_obj_t* clockLabel;
static lv_obj_t* speedLabel;
static lv_obj_t* footCol;
static lv_obj_t* secRow;
static lv_obj_t* modeChip;
static lv_obj_t* secKeyLabel;
static lv_obj_t* secValLabel;

// The secondary value the foot shows (issue #142): session-level values from
// the stats engine. Vertical scroll on the tile cycles 10s -> MAX -> AVG ->
// off. UI-local preference — deliberately not persisted.
enum class Secondary : uint8_t { TEN_S, MAX, AVG, OFF };
static Secondary secondary = Secondary::TEN_S;

// Names come from the shared protocol vocabulary (issue #146) so the chip,
// the picker and the sidecar can't drift apart. UNKNOWN gets a dim
// placeholder: the boat hasn't applied any mode event since boot (or we've
// never heard from it) — showing a mode would be a guess.
static const char* modeText(uint8_t mode) {
  if (!moana::isSelectableSailingMode(mode)) {
    return "- - -";
  }
  return moana::sailingModeName(mode);
}

// Vertical swipe cycles the secondary value. Only TOP/BOTTOM gestures are
// handled — LEFT/RIGHT belong to the tileview's horizontal carousel swipe,
// so the two can't fight.
static void onGesture(lv_event_t* e) {
  (void)e;
  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return;
  }
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_TOP) {
    secondary = (Secondary)(((uint8_t)secondary + 1) % 4);
  } else if (dir == LV_DIR_BOTTOM) {
    secondary = (Secondary)(((uint8_t)secondary + 3) % 4);
  }
}

// An invisible auto-sized flex box: no background, border or padding, so only
// its children show and the box re-centres itself as they change width.
static lv_obj_t* flexBox(lv_obj_t* parent, lv_flex_flow_t flow, int gap) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_pad_column(box, gap, 0);
  lv_obj_set_style_pad_row(box, gap, 0);
  lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, flow);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  return box;
}

void create(lv_obj_t* tile) {
  // Local time, under the header's icon row (see kClockTop).
  clockLabel = lv_label_create(tile);
  lv_obj_set_style_text_font(clockLabel, &font_time_56, 0);
  lv_obj_set_style_text_color(clockLabel, lv_color_hex(kColDim), 0);
  lv_obj_align(clockLabel, LV_ALIGN_TOP_MID, 0, kClockTop);
  lv_label_set_text(clockLabel, "--:--");

  // Speed: centred in the full-width middle band, which clears the corner
  // arcs by construction (band starts below the top radius).
  speedLabel = lv_label_create(tile);
  lv_obj_set_style_text_font(speedLabel, &font_speed_170, 0);
  lv_obj_set_style_text_color(speedLabel, lv_color_white(), 0);
  lv_obj_align(speedLabel, LV_ALIGN_CENTER, 0, kSpeedOffsetY);
  lv_label_set_text(speedLabel, "--");

  // Speed foot: mode chip on one line, secondary value on the next, both
  // centred (each flex box re-centres itself as label widths change).
  footCol = flexBox(tile, LV_FLEX_FLOW_COLUMN, 6);
  lv_obj_align(footCol, LV_ALIGN_BOTTOM_MID, 0, kFootOffsetY);

  // Mode chip (issue #139): amber label, mockups' .mode-chip styling.
  modeChip = lv_label_create(footCol);
  lv_obj_set_style_text_font(modeChip, &font_ui_28, 0);
  lv_obj_set_style_text_color(modeChip, lv_color_hex(kColFaint), 0);
  lv_obj_set_style_text_letter_space(modeChip, 4, 0);
  lv_label_set_text(modeChip, modeText(moana::MODE_UNKNOWN));

  // Secondary value (issue #142): dim key + white value, mockups' .sec-val.
  // Its own row under the mode, hidden as a unit when cycled off.
  secRow = flexBox(footCol, LV_FLEX_FLOW_ROW, 16);

  // White, not the mockups' dim grey: the key is the only thing saying WHICH
  // stat the number beside it is, and a value you can't name is no use
  // (issue #159). Size still separates the two.
  secKeyLabel = lv_label_create(secRow);
  lv_obj_set_style_text_font(secKeyLabel, &font_ui_24, 0);
  lv_obj_set_style_text_color(secKeyLabel, lv_color_white(), 0);
  lv_obj_set_style_text_letter_space(secKeyLabel, 2, 0);
  lv_label_set_text(secKeyLabel, "10s");

  secValLabel = lv_label_create(secRow);
  lv_obj_set_style_text_font(secValLabel, &font_ui_28, 0);
  lv_obj_set_style_text_color(secValLabel, lv_color_white(), 0);
  lv_label_set_text(secValLabel, "--");

  // Vertical swipe anywhere on the tile cycles the secondary value. LVGL
  // delivers LV_EVENT_GESTURE to the first ancestor WITHOUT
  // LV_OBJ_FLAG_GESTURE_BUBBLE (normally the screen), so the tile drops the
  // flag to become that target. Horizontal pans grab the tileview as
  // scroll_obj first, which suppresses the gesture entirely — the carousel
  // swipe can't fight this.
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(tile, onGesture, LV_EVENT_GESTURE, nullptr);
}

void refresh(const model::AppModel& m) {
  // Time comes from the timebase, not from the packet in hand (issue #144):
  // the RTC keeps it running through a boat that is off, out of range or
  // simply not aboard. `--:--` now means one thing only — nothing has ever
  // set this watch's clock.
  if (m.utcSeconds != 0) {
    uint32_t local =
        m.utcSeconds + (uint32_t)nz_time::utcOffsetSeconds(m.utcSeconds);
    lv_label_set_text_fmt(clockLabel, "%02u:%02u",
                          (unsigned)(local / 3600 % 24),
                          (unsigned)(local / 60 % 60));
  } else {
    lv_label_set_text(clockLabel, "--:--");
  }

  if (!m.haveTelemetry) {
    lv_label_set_text(speedLabel, "--");
  } else {
    lv_label_set_text_fmt(speedLabel, "%.1f", m.telemetry.sog_knots);
  }
  // Stale data stays on screen but visibly demoted. Greys at link LOST
  // (>10 s), matching the pre-carousel behaviour — the amber link icon
  // already tells the 3 s story. Amber while live but not recording (issue
  // #184): a whole sail once went unrecorded behind white digits, and these
  // are the pixels every glance lands on.
  bool stale = !m.haveTelemetry || m.link == model::LinkState::LOST;
  lv_color_t speedColor = stale            ? lv_color_hex(kColStale)
                          : m.recording() ? lv_color_white()
                                          : lv_color_hex(kColAmber);
  lv_obj_set_style_text_color(speedLabel, speedColor, 0);

  // Mode chip shows the boat's echo only (never the last local press):
  // amber when known, faint placeholder while UNKNOWN, and greyed alongside
  // the digits when the link is lost — the echo is no longer current truth.
  // While the boat is not recording it says so instead: a mode labels a leg,
  // and there is no leg (issue #184). The Mode tile still shows the echo.
  // Tracking stays constant now the mode has a line to itself: the nine-letter
  // names (MOTORSAIL, UNPOWERED, ALONGSIDE, TRAILERED) only needed tightening
  // because a secondary value shared the row and pushed them into the arcs.
  bool notRecording = m.haveTelemetry && !m.recording();
  lv_label_set_text(modeChip,
                    notRecording ? "NOT RECORDING" : modeText(m.modeEcho));
  bool known = notRecording || m.modeEcho != moana::MODE_UNKNOWN;
  lv_obj_set_style_text_color(
      modeChip,
      lv_color_hex(known && !stale ? kColAmber : kColFaint), 0);

  // Secondary value: session-level stats from the engine (issue #142).
  if (secondary == Secondary::OFF) {
    lv_obj_add_flag(secRow, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(secRow, LV_OBJ_FLAG_HIDDEN);

  stats_engine::SessionView sv = m.stats->session();
  const char* key = "10s";
  float value = 0;
  bool has = false;
  switch (secondary) {
    case Secondary::TEN_S:
      key = "10s";
      value = sv.best10sKn;
      has = sv.exists && sv.hasBest10s;
      break;
    case Secondary::MAX:
      key = "MAX";
      value = sv.maxKn;
      has = sv.exists && sv.hasMax;
      break;
    case Secondary::AVG:
      key = "AVG";
      value = sv.avgKn;
      has = sv.exists;
      break;
    case Secondary::OFF:
      break;
  }
  lv_label_set_text(secKeyLabel, key);
  if (has) {
    lv_label_set_text_fmt(secValLabel, "%.1f", (double)value);
  } else {
    lv_label_set_text(secValLabel, "--");
  }
  lv_obj_set_style_text_color(
      secValLabel, stale ? lv_color_hex(kColStale) : lv_color_white(), 0);
}

}  // namespace speed_screen
