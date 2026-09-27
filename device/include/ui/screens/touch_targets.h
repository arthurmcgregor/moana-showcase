#pragma once

#include "ui/screens/screen_shared.h"

// Pure touch target geometry — no Display, no ButtonId dependencies.
namespace TouchTargets {

// Main menu: 2x2 grid of buttons below the status bar.
// Returns the slot index of the tapped quadrant:
//   0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right
// The screen decides what each slot means based on its menu table + page.
// Returns -1 for taps in the status bar (y < STATUS_BAR_HEIGHT); the caller
// must treat a negative slot as "no button".
inline int mainMenuSlot(int x, int y, int screenWidth, int screenHeight) {
  // Reject the status-bar strip: without this, adjustedY goes negative, topHalf
  // is true, and a tap on the clock/battery activates slot 0/1 (B8).
  if (y < STATUS_BAR_HEIGHT) return -1;

  int contentHeight = screenHeight - STATUS_BAR_HEIGHT;
  int buttonWidth = screenWidth / 2;
  int buttonHeight = contentHeight / 2;

  int adjustedY = y - STATUS_BAR_HEIGHT;
  bool leftSide = x < buttonWidth;
  bool topHalf = adjustedY < buttonHeight;

  if (topHalf && leftSide) return 0;
  if (topHalf && !leftSide) return 1;
  if (!topHalf && leftSide) return 2;
  return 3;
}

// Single-button screens (wifi, sync): button at fixed position.
// Returns true if the tap is within the button bounds.
inline bool isInsideButton(int x, int y, int buttonX, int buttonY, int buttonWidth,
                           int buttonHeight) {
  return x >= buttonX && x <= buttonX + buttonWidth && y >= buttonY &&
         y <= buttonY + buttonHeight;
}

}  // namespace TouchTargets
