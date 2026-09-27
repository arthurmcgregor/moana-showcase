#pragma once

namespace ui {

// Build the UI: floating header on lv_layer_top(), horizontal tileview
// carousel (Speed | Stats | Control), and one LVGL refresh timer that builds
// the AppModel snapshot, ticks the command channel + stats engine, and
// refreshes the active screen. Call after beginLvglHelper(); no per-loop
// update call is needed.
void begin();

}  // namespace ui
