#include "header.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>

#include "fonts.h"

namespace header {

// The AMOLED's rounded corners (~70 px) hide the outermost pixels. All
// header items cluster around the panel's horizontal centre so the top row
// stays inside the corner arcs (see watch/CLAUDE.md).
constexpr int kHeaderPadTop = 14;
// ±122 clipped time/battery on real glass — the ~70px radius constant
// understates the visible arc. ±100 verified clear.
//
// The three status items space evenly across that ±100 band. The clock used to
// share the row, then took a second header line of its own (issue #159); it now
// belongs to the Speed tile instead — the only screen with room for it, and the
// only one where a glance at the time is part of the same glance as the speed.
constexpr int kSdOffsetX = -100;
constexpr int kLinkOffsetX = 0;
constexpr int kBattOffsetX = 100;

// Per-icon Y nudge so the drawn content's bottom lines up with the label
// glyph bottoms (~kHeaderPadTop + 26): the rotated SD's ink ends at canvas
// y=23, the link icon's antenna dot at y=25.
constexpr int kSdOffsetY = 3;
constexpr int kLinkOffsetY = 1;

// Palette (docs/watch-screen-mockups.html).
constexpr uint32_t kColDim = 0xAAB1BA;
constexpr uint32_t kColFaint = 0x59606A;
constexpr uint32_t kColRec = 0xFF4D4F;
constexpr uint32_t kColAmber = 0xFFB648;

constexpr int kIconSize = 28;

// The mismatch note hangs one line below the icon row, centred, and only
// exists while there is a mismatch to report (issue #173). It clears the
// corner arcs at ~±95 px and overlaps the top-anchored tiles (y=60) by a few
// pixels — it draws on lv_layer_top, above them, and the screens underneath
// are greyed out anyway, because a link we cannot read is a link that is
// lost. Worth the trespass: it is the only thing on the watch that can tell
// you the boat is fine and the flash is half-finished.
constexpr int kLinkNoteOffsetY = 30;

static lv_obj_t* battLabel;
static lv_obj_t* sdCanvas;
static lv_obj_t* linkCanvas;
static lv_obj_t* linkNote;

// Icons are redrawn only on a state change (cheap: a few rects on a 28 px
// canvas), which sidesteps LVGL image-recolor bleeding into the punched-out
// black details (SD contact stripes, slash gap).
static bool sdRecording = false;
static model::LinkState linkDrawn = model::LinkState::FRESH;
static bool mismatchDrawn = false;
static bool iconsDrawn = false;

LV_DRAW_BUF_DEFINE_STATIC(sdBuf, 28, 28, LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(linkBuf, 28, 28, LV_COLOR_FORMAT_ARGB8888);

static void fillRect(lv_layer_t* layer, int x1, int y1, int x2, int y2,
                     lv_color_t color, int radius) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = radius;
  lv_area_t area = {x1, y1, x2, y2};
  lv_draw_rect(layer, &dsc, &area);
}

// SD card lying on its side (rotated 90° cw: contacts right, corner cut
// top-right), three contact stripes punched out in black (the screen behind
// is always black). White while recording, faint grey when idle — red
// proved distracting on the wrist.
static void drawSdIcon(bool recording) {
  lv_color_t color = recording ? lv_color_white() : lv_color_hex(kColFaint);

  lv_canvas_fill_bg(sdCanvas, lv_color_black(), LV_OPA_TRANSP);
  lv_layer_t layer;
  lv_canvas_init_layer(sdCanvas, &layer);

  // Body: bottom part full width, top part stops short of the corner cut,
  // and a triangle joining them across the diagonal.
  fillRect(&layer, 0, 10, 26, 23, color, 0);
  fillRect(&layer, 0, 4, 20, 10, color, 0);
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = color;
  tri.opa = LV_OPA_COVER;
  tri.p[0] = {20, 4};
  tri.p[1] = {26, 10};
  tri.p[2] = {20, 10};
  lv_draw_triangle(&layer, &tri);

  // Contact stripes.
  lv_color_t hole = lv_color_black();
  fillRect(&layer, 19, 12, 24, 13, hole, 0);
  fillRect(&layer, 19, 15, 24, 16, hole, 0);
  fillRect(&layer, 19, 18, 24, 19, hole, 0);

  lv_canvas_finish_layer(sdCanvas, &layer);
  lv_obj_invalidate(sdCanvas);
}

// Signal arcs: antenna dot + two broadcast arcs opening upward. White when
// fresh, amber when stale, faint grey with a red slash when lost — and amber
// *with* the slash on a protocol mismatch (issue #173), which is the one
// case where the boat is provably transmitting and provably unreadable. The
// slash says the link is unusable, the amber says something is arriving; grey
// and slashed, the state this is easily mistaken for, means silence.
static void drawLinkIcon(model::LinkState state, bool mismatch) {
  lv_color_t color;
  switch (state) {
    case model::LinkState::FRESH:
      color = lv_color_white();
      break;
    case model::LinkState::STALE:
      color = lv_color_hex(kColAmber);
      break;
    case model::LinkState::LOST:
    default:
      color = lv_color_hex(mismatch ? kColAmber : kColFaint);
      break;
  }

  lv_canvas_fill_bg(linkCanvas, lv_color_black(), LV_OPA_TRANSP);
  lv_layer_t layer;
  lv_canvas_init_layer(linkCanvas, &layer);

  // Antenna dot.
  fillRect(&layer, 11, 19, 17, 25, color, LV_RADIUS_CIRCLE);

  // Two arcs centred on the dot. LVGL angles: 0° = 3 o'clock, clockwise;
  // 225°→315° spans ±45° around straight up.
  for (int radius = 9; radius <= 15; radius += 6) {
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = color;
    arc.width = 3;
    arc.rounded = 1;
    arc.center = {14, 22};
    arc.radius = radius;
    arc.start_angle = 225;
    arc.end_angle = 315;
    lv_draw_arc(&layer, &arc);
  }

  if (state == model::LinkState::LOST || mismatch) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(kColRec);
    line.width = 3;
    line.round_start = 1;
    line.round_end = 1;
    line.p1 = {4, 25};
    line.p2 = {24, 3};
    lv_draw_line(&layer, &line);
  }

  lv_canvas_finish_layer(linkCanvas, &layer);
  lv_obj_invalidate(linkCanvas);
}

void create() {
  lv_obj_t* top = lv_layer_top();

  sdCanvas = lv_canvas_create(top);
  LV_DRAW_BUF_INIT_STATIC(sdBuf);
  lv_canvas_set_draw_buf(sdCanvas, &sdBuf);
  lv_obj_remove_flag(sdCanvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(sdCanvas, LV_ALIGN_TOP_MID, kSdOffsetX - kIconSize / 2,
               kHeaderPadTop + kSdOffsetY);

  linkCanvas = lv_canvas_create(top);
  LV_DRAW_BUF_INIT_STATIC(linkBuf);
  lv_canvas_set_draw_buf(linkCanvas, &linkBuf);
  lv_obj_remove_flag(linkCanvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(linkCanvas, LV_ALIGN_TOP_MID, kLinkOffsetX - kIconSize / 2,
               kHeaderPadTop + kLinkOffsetY);

  linkNote = lv_label_create(top);
  lv_obj_set_style_text_font(linkNote, &font_ui_24, 0);
  lv_obj_set_style_text_color(linkNote, lv_color_hex(kColAmber), 0);
  lv_obj_align(linkNote, LV_ALIGN_TOP_MID, kLinkOffsetX,
               kHeaderPadTop + kLinkNoteOffsetY);
  lv_label_set_text(linkNote, "");
  lv_obj_add_flag(linkNote, LV_OBJ_FLAG_HIDDEN);

  battLabel = lv_label_create(top);
  lv_obj_set_style_text_font(battLabel, &font_ui_28, 0);
  lv_obj_set_style_text_color(battLabel, lv_color_hex(kColDim), 0);
  lv_obj_align(battLabel, LV_ALIGN_TOP_MID, kBattOffsetX, kHeaderPadTop);
  lv_label_set_text(battLabel, "");
}

void refresh(const model::AppModel& m) {
  lv_label_set_text_fmt(battLabel, "%d%%%s", m.batteryPercent,
                        m.charging ? "+" : "");

  // SD icon follows the boat's echoed recording state only.
  bool recording = m.recording();
  if (!iconsDrawn || recording != sdRecording) {
    sdRecording = recording;
    drawSdIcon(recording);
  }
  if (!iconsDrawn || m.link != linkDrawn ||
      m.protocolMismatch != mismatchDrawn) {
    linkDrawn = m.link;
    mismatchDrawn = m.protocolMismatch;
    drawLinkIcon(m.link, m.protocolMismatch);
  }
  iconsDrawn = true;

  // Spelled out rather than symbolised: the numbers are what tell you which
  // end is stale, and `≠` isn't in font_ui_24's ASCII range — it would draw
  // as a hollow box (see watch/CLAUDE.md on the generated fonts).
  if (m.protocolMismatch) {
    lv_label_set_text_fmt(linkNote, "RX v%u WANT v%u", m.mismatchVersion,
                          moana::kProtocolVersion);
    lv_obj_remove_flag(linkNote, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(linkNote, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace header
