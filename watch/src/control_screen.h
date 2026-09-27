#pragma once

#include <lvgl.h>

#include "app_model.h"

// Tile 1 of the carousel: recording control (issue #138, mockups in
// docs/watch-screen-mockups.html). State label driven purely by the boat's
// telemetry echo, hold-to-confirm button with a 1 s fill sweep (release
// early cancels), and meta lines (last-packet age, queue depth).
namespace control_screen {

// Called when a hold completes; desiredOn is the absolute state to request
// (derived from the action the button was showing: start -> true,
// stop/reset -> false).
using HoldConfirmedFn = void (*)(bool desiredOn);

void create(lv_obj_t* tile, HoldConfirmedFn onHoldConfirmed);
void refresh(const model::AppModel& m);

}  // namespace control_screen
