#pragma once

#include "app_model.h"

// Floating header, built once on lv_layer_top() so it sits above every tile
// of the carousel — no per-tile duplication. One row of status, centre-anchored
// to clear the ~70 px glass corner arcs: SD-card icon (grey idle / white
// recording), signal-arcs link icon (white ok / amber stale / grey with a red
// slash when lost, amber with the slash when the boat speaks a protocol
// version we don't), battery %, and — only while that mismatch is live — a
// second line naming both versions. The clock is NOT here — it belongs to the
// Speed tile alone (see speed_screen).
namespace header {

void create();
void refresh(const model::AppModel& m);

}  // namespace header
