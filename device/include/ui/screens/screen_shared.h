#pragma once

#include <cstdint>

inline constexpr int STATUS_BAR_HEIGHT = 30;

enum class ButtonColor { ButtonGreen, ButtonGrey, ButtonRed };

// RGB565 cues for GPS-fix staleness on the live data screens (R8). Live data
// is green; when the fix is LOST the last-known values are dimmed to grey and
// an amber "NO FIX" caption is shown, so a frozen reading can't be mistaken
// for a live one. Defined explicitly rather than relying on framework colour
// macros so the meaning travels with the value.
inline constexpr uint16_t COLOR_LIVE = 0x07E0;   // green
inline constexpr uint16_t COLOR_STALE = 0x7BEF;  // dim grey
inline constexpr uint16_t COLOR_WARN = 0xFD20;   // amber
