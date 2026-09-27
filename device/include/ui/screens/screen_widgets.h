#pragma once

#include "ui/screens/screen_shared.h"
#include "ui/text_style.h"

// Shared screen-drawing widgets. Device-only (uses M5.Display) — not part of
// the host build. Consolidates the button painter and content-clear that were
// copy-pasted across every screen (F3).
namespace ScreenWidgets {

// Filled button: outer border rectangle, inner background rectangle, centred
// label. Geometry (4 px border pad + 2 px inner pad) and the ButtonColor
// palette match the former per-screen painters. The label style defaults to
// Title (main menu / wifi / sync); the calibration screen passes Body for its
// longer labels.
void drawFilledButton(int x, int y, int w, int h, const char* label, ButtonColor color,
                      ui::TextStyle labelStyle = ui::TextStyle::Title);

// Clear the content area below the status bar to black.
void clearContent();

}  // namespace ScreenWidgets
