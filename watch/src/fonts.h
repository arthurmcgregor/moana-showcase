#pragma once

#include <lvgl.h>

// Montserrat Medium with tabular figures frozen in (tools/fonts/build.sh).
// LVGL's built-in lv_font_montserrat_* are the same typeface but proportional:
// '1' is ~half the advance of '0', so any label whose digits change — SOG, the
// stats grid, the clock — reflows on every update. These replace them.
//
// The device renders the same frozen TTF through M5GFX (device/src/ui/fonts/),
// so both screens are one typeface.
LV_FONT_DECLARE(font_ui_24)
LV_FONT_DECLARE(font_ui_28)
LV_FONT_DECLARE(font_ui_40)

// Digits-only ('0-9', '.', '-') for the SOG readout.
LV_FONT_DECLARE(font_speed_170)

// Header clock: '0-9', ':' and the '-' of the "--:--" placeholder. Same
// reasoning as the SOG readout — ASCII at this size would be ~330KB of source
// for glyphs a clock never draws.
LV_FONT_DECLARE(font_time_56)
