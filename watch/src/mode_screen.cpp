#include "mode_screen.h"

#include "fonts.h"

namespace mode_screen {

// Palette (docs/watch-screen-mockups.html).
constexpr uint32_t kColDim = 0xAAB1BA;
constexpr uint32_t kColFaint = 0x59606A;
constexpr uint32_t kColAmber = 0xFFB648;

// The picker's vocabulary: every server Mode.MODE_CHOICES value, in the
// protocol enum's order (which is MODE_CHOICES' order). MODE_UNKNOWN is
// deliberately absent — it is an echo state, not something to select.
constexpr uint8_t kModes[] = {
    moana::MODE_SAIL,      moana::MODE_MOTORSAIL, moana::MODE_MOTOR,
    moana::MODE_ROW,       moana::MODE_UNPOWERED, moana::MODE_ANCHOR,
    moana::MODE_ALONGSIDE, moana::MODE_BEACHED,   moana::MODE_TRAILERED,
    moana::MODE_OTHER,
};
constexpr size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// Rows are sized for wet fingers first: 64 px is ~7 mm on this 410x502 panel,
// well past the 5 mm rule of thumb, and the 8 px gap keeps a sloppy tap from
// landing between two modes. 330 px wide matches the Stats session line and
// stays inside the glass corner arcs even at the ends of the scroll range.
constexpr int kRowHeight = 64;
constexpr int kRowGap = 8;
constexpr int kListWidth = 330;
// Starts below the floating header (which lives on lv_layer_top() and would
// otherwise be scrolled under) and stops clear of the bottom arcs. Only the
// icon row is up there now (ending around y=42) — the clock line that used to
// force this down to 100 moved to the Speed tile.
//
// The bottom margin is doing more than clearing the arcs (330 px wide, the
// corner cuts in ~7 px): at 16 the window came to 426 px, which is *exactly*
// six rows and no more, leaving nothing to say the other four modes are down
// there. 30 keeps the sixth row half-shown, which is the whole cue that the
// list scrolls, and still shows more of it than the old 100/16 pair did.
constexpr int kListTop = 60;
constexpr int kListBottomMargin = 30;

static lv_obj_t* rows[kModeCount];
static lv_obj_t* labels[kModeCount];
static PickFn pickCb = nullptr;

// Style memo: refresh() runs 4/s, so only repaint when the picture actually
// changes (10 rows × several style calls is not free, and needless
// invalidation redraws the whole tile).
static uint8_t shownEcho = 0xFF;
static uint8_t shownRequested = 0xFF;
static bool shownPending = false;
static bool shownStale = false;
static bool shownAny = false;

static void onRowClicked(lv_event_t* e) {
  // LVGL only sends CLICKED when the press did not scroll anything, so a
  // vertical list drag or a horizontal carousel swipe can't pick a mode.
  uint8_t mode = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (pickCb) {
    pickCb(mode);
  }
}

void create(lv_obj_t* tile, PickFn onPick) {
  pickCb = onPick;

  // Scrollable column. LV_DIR_VER only: LVGL walks up to the nearest ancestor
  // scrollable in the drag's direction, so horizontal pans still reach the
  // tileview and the carousel keeps working over the list.
  lv_obj_t* list = lv_obj_create(tile);
  lv_obj_set_size(list, kListWidth, LV_VER_RES - kListTop - kListBottomMargin);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, kListTop);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, kRowGap, 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  for (size_t i = 0; i < kModeCount; i++) {
    rows[i] = lv_button_create(list);
    lv_obj_set_size(rows[i], LV_PCT(100), kRowHeight);
    lv_obj_set_style_radius(rows[i], kRowHeight / 2, 0);
    lv_obj_set_style_shadow_width(rows[i], 0, 0);
    lv_obj_set_style_pad_all(rows[i], 0, 0);
    lv_obj_add_event_cb(rows[i], onRowClicked, LV_EVENT_CLICKED,
                        (void*)(uintptr_t)kModes[i]);

    // Explicit pressed fill: refresh() drives bg_opa on the default state,
    // which would otherwise mask the theme's pressed colour and leave a tap
    // looking inert until the haptic fires.
    lv_obj_set_style_bg_color(rows[i], lv_color_hex(kColAmber),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(rows[i], LV_OPA_40, LV_STATE_PRESSED);

    labels[i] = lv_label_create(rows[i]);
    lv_obj_set_style_text_font(labels[i], &font_ui_28, 0);
    lv_obj_set_style_text_letter_space(labels[i], 2, 0);
    lv_obj_center(labels[i]);
    lv_label_set_text(labels[i], moana::sailingModeName(kModes[i]));
  }
}

void refresh(const model::AppModel& m) {
  // A lost link means the echo is no longer current truth — same demotion the
  // Speed chip and digits use, so a stale ANCHOR doesn't read as confirmed.
  bool stale = !m.haveTelemetry || m.link == model::LinkState::LOST;
  // A request only counts as in flight while it disagrees with the echo:
  // re-asserting the current mode (the PWR leg cut) must not outline a row.
  bool pending = m.modeRequestPending && m.modeRequested != m.modeEcho &&
                 moana::isSelectableSailingMode(m.modeRequested);

  if (shownAny && m.modeEcho == shownEcho && m.modeRequested == shownRequested &&
      pending == shownPending && stale == shownStale) {
    return;
  }
  bool echoChanged = !shownAny || m.modeEcho != shownEcho;
  shownAny = true;
  shownEcho = m.modeEcho;
  shownRequested = m.modeRequested;
  shownPending = pending;
  shownStale = stale;

  for (size_t i = 0; i < kModeCount; i++) {
    bool isEcho = kModes[i] == m.modeEcho;
    bool isPending = pending && kModes[i] == m.modeRequested;

    // Applied mode: filled amber. Queued tap: amber outline, no fill — the
    // boat has not confirmed it yet and the row must not claim otherwise.
    // Everything else: hairline outline, dim label.
    lv_color_t accent =
        lv_color_hex(isEcho && stale ? kColFaint : kColAmber);
    lv_obj_set_style_bg_color(rows[i], accent, 0);
    lv_obj_set_style_bg_opa(rows[i], isEcho && !stale ? LV_OPA_20 : LV_OPA_TRANSP,
                            0);
    lv_obj_set_style_border_width(rows[i], isEcho || isPending ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        rows[i], isEcho || isPending ? accent : lv_color_hex(kColFaint), 0);
    lv_obj_set_style_text_color(
        labels[i], isEcho || isPending ? accent : lv_color_hex(kColDim), 0);
  }

  // Bring the boat's mode into view when it changes (ANCHOR and OTHER are off
  // the bottom of the window). Only on change, so it never fights a scroll in
  // progress.
  if (echoChanged) {
    for (size_t i = 0; i < kModeCount; i++) {
      if (kModes[i] == m.modeEcho) {
        lv_obj_scroll_to_view(rows[i], LV_ANIM_ON);
        break;
      }
    }
  }
}

}  // namespace mode_screen
