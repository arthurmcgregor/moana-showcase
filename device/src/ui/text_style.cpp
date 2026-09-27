#include "ui/text_style.h"

#include <M5Unified.h>

#include "ui/fonts/montserrat_12.h"
#include "ui/fonts/montserrat_16.h"
#include "ui/fonts/montserrat_24.h"
#include "ui/fonts/montserrat_32.h"
#include "ui/fonts/montserrat_digits_136.h"
#include "utils/logger.h"

static const char* LOG_TAG = "TextStyle";

namespace ui {

namespace {

const uint8_t* fontFor(TextStyle style) {
  switch (style) {
    case TextStyle::Fine:
      return montserrat_12;
    case TextStyle::Body:
      return montserrat_16;
    case TextStyle::Title:
      return montserrat_24;
    case TextStyle::Large:
      return montserrat_32;
    case TextStyle::Speed:
      return montserrat_digits_136;
  }
  return montserrat_16;
}

// loadFont() re-parses the glyph table and reallocates its metric arrays on
// every call, so the redraw paths (which set a style per string) would thrash
// the heap without this. Only valid while applyTextStyle is the single path to
// font selection — see the header.
bool applied = false;
TextStyle current = TextStyle::Body;

}  // namespace

void applyTextStyle(TextStyle style) {
  if (applied && style == current) {
    M5.Display.setTextSize(1);
    return;
  }

  if (M5.Display.loadFont(fontFor(style))) {
    applied = true;
    current = style;
  } else {
    // unloadFont() inside the loader has already reverted us to Font0, so text
    // still draws — just blocky and at the wrong size. Worth a log line.
    LOG_WARN(LOG_TAG, "VLW load failed for style %d, falling back to Font0",
             static_cast<int>(style));
    applied = false;
  }

  M5.Display.setTextSize(1);
}

}  // namespace ui
