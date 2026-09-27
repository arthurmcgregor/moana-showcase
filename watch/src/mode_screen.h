#pragma once

#include <lvgl.h>

#include "app_model.h"

// Mode tile of the carousel (issue #146): a scrollable row per server mode,
// so all ten of Mode.MODE_CHOICES can be set on the water — the GPIO0 buttons
// only cover the quick SAIL↔MOTOR toggle and OTHER.
//
// Why a carousel tile rather than a tap on the Speed screen's mode chip: the
// Speed tile already owns vertical gestures (they cycle the secondary value),
// so a chip tap would sit inside a gesture surface, and a mis-registered swipe
// would silently set a mode. A tile of its own also means the picker needs no
// open/close state — swiping away *is* the cancel, and LVGL suppresses the
// row CLICKED event whenever a drag scrolled anything, so swiping past this
// tile can never set a mode by accident.
//
// The rows never lead the boat: the echoed mode is the filled row, and a tap
// that is still queued (out of range, or not yet applied) shows as an outline
// only. Selecting a mode queues the event and returns to Speed, where the
// chip confirms on the echo.
namespace mode_screen {

// Called on a row tap with the moana::SailingMode picked.
using PickFn = void (*)(uint8_t mode);

void create(lv_obj_t* tile, PickFn onPick);
void refresh(const model::AppModel& m);

}  // namespace mode_screen
