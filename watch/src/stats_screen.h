#pragma once

#include <lvgl.h>

#include "app_model.h"

// Stats tile of the carousel (issue #142, mockups in
// docs/watch-screen-mockups.html): current leg primary (LEG n + mode + leg
// time), DIST/AVG/MAX/10s grid, pinned session-total line. Vertical scroll
// browses past legs (previous leg peeks above); the session line stays
// pinned. Holding the session line for 1 s (fill sweep, control_screen's
// hold pattern) requests a mid-session stats reset.
namespace stats_screen {

// Called when the 1 s hold on the session line completes; ui.cpp resolves it
// against the engine (reset while live, error haptic otherwise).
using ResetHoldFn = void (*)();

void create(lv_obj_t* tile, ResetHoldFn onResetHold);
void refresh(const model::AppModel& m);

// Called when the carousel lands on the Stats tile. Drops any pinned past
// leg so the screen always opens on the current one (issue #159) — a browse
// left behind from last time would otherwise mean scrolling back through
// however many legs have been cut since.
void onShown();

}  // namespace stats_screen
