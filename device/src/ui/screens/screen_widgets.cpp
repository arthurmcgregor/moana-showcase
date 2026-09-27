#include "ui/screens/screen_widgets.h"

#include <M5Unified.h>

#include "ui/text_style.h"

namespace ScreenWidgets {

void drawFilledButton(int x, int y, int w, int h, const char* label, ButtonColor color,
                      ui::TextStyle labelStyle) {
  // Default to the grey palette so the vars are never read uninitialised for an
  // out-of-range colour (the switch has no default case).
  uint16_t bg = TFT_DARKGREY;
  uint16_t border = TFT_LIGHTGREY;
  uint16_t text = TFT_LIGHTGREY;
  switch (color) {
    case ButtonColor::ButtonGrey:
      bg = TFT_DARKGREY; border = TFT_LIGHTGREY; text = TFT_LIGHTGREY; break;
    case ButtonColor::ButtonRed:
      bg = TFT_MAROON; border = TFT_RED; text = TFT_WHITE; break;
    case ButtonColor::ButtonGreen:
      bg = TFT_DARKGREEN; border = TFT_GREEN; text = TFT_WHITE; break;
  }

  constexpr int pad = 4;
  constexpr int inner = 2;
  M5.Display.fillRect(x + pad, y + pad, w - 2 * pad, h - 2 * pad, border);
  M5.Display.fillRect(x + pad + inner, y + pad + inner, w - 2 * (pad + inner),
                      h - 2 * (pad + inner), bg);
  ui::applyTextStyle(labelStyle);
  M5.Display.setTextColor(text);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

void clearContent() {
  M5.Display.fillRect(0, STATUS_BAR_HEIGHT, M5.Display.width(),
                      M5.Display.height() - STATUS_BAR_HEIGHT, TFT_BLACK);
}

}  // namespace ScreenWidgets
