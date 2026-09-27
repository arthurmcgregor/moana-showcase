#pragma once

#include <lvgl.h>

#include "app_model.h"

// Tile 0 of the carousel: SOG readout in font_speed_170 digits, greyed when
// the link is stale/lost (data stays visible but visibly demoted), the local
// clock above it, and the mode chip + one secondary stat below.
namespace speed_screen {

void create(lv_obj_t* tile);
void refresh(const model::AppModel& m);

}  // namespace speed_screen
