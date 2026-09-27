#pragma once

#include <cstdint>

namespace ui {

// The device renders Montserrat Medium with tabular figures frozen in — the
// same typeface and the same frozen TTF the watch uses, so a speed read the
// same on both screens. Regenerate the assets with tools/fonts/build.sh.
//
// Named styles rather than pixel sizes: M5GFX's setTextSize() multiplier was
// only ever meaningful against the old 5x7 bitmap Font0 (8 px cell x N), and
// scaling a VLW bitmap font up blurs it. Each style maps to a font baked at
// that exact size instead. Sizes track what the Font0 multiples used to render.
enum class TextStyle : uint8_t {
  Fine,   // 12 px — footnotes (was setTextSize(1), 8 px)
  Body,   // 16 px — status bar, status lines (was 2)
  Title,  // 24 px — screen titles, detail rows, buttons (was 3)
  Large,  // 32 px — calibration countdown (was 4)
  Speed,  // 136 px, digits only — speed screen (was 17)
};

// Selects the font for `style` and resets the size multiplier to 1. Re-applying
// the current style is a no-op, so callers can be liberal. If a font fails to
// load — the loader allocates its metric tables on the heap — the display falls
// back to Font0 rather than drawing nothing.
//
// Every font selection on the device must go through here; a bare setFont()
// elsewhere would desync the "already applied" check.
void applyTextStyle(TextStyle style);

}  // namespace ui
