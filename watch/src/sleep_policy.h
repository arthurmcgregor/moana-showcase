#pragma once

#include <stdint.h>

// Display sleep/wake policy, extracted from the UI refresh timer so wake
// sources can grow (buttons, raise-to-wake) without touching screen code.
//
// The panel sleeps after 60 s of LVGL input inactivity and any touch wakes
// it. Deliberately sleepDisplay() (~10 mA, touch controller stays awake) and
// not instance.lightSleep(): light sleep drops the WiFi radio, killing
// ESP-NOW telemetry, and doesn't re-init WiFi/ESP-NOW on wake.
namespace sleep_policy {

// Call once, after beginLvglHelper() has created the touch input device.
// Installs the wake-touch gate: the stroke that wakes a dark panel is
// swallowed below LVGL, so it reaches no screen at all (issue #159). Without
// it a swipe on a dark screen wakes the panel AND pages the carousel, and
// every tap-to-act control has to defend itself individually.
void begin();

// Call once per UI tick. Returns true while the screen is asleep — the
// caller should skip all refresh work (no label updates -> no invalidation
// -> no redraw work).
bool tick();

// Register non-touch user activity (e.g. a GPIO button press) with the
// policy: resets the LVGL inactivity clock, so a sleeping screen wakes on
// the next tick and an awake one stays awake.
void noteActivity();

// True when the touch being handled is the one that woke (or is waking) the
// panel, rather than a deliberate tap on what is under the finger. The gate
// installed by begin() already stops a wake stroke reaching any screen, so
// this is now a second line of defence rather than the only one — kept
// because a tap-to-ACT control acting on a wake would be the worst kind of
// bug to reintroduce silently (on the Mode tile that means setting a sailing
// mode blind, issue #146). Hold-to-confirm controls don't need it; a 1 s hold
// is never a wake tap.
bool touchIsWake(uint32_t nowMs);

// How long the panel has been dark, in ms; 0 while awake. Lets the UI act on
// a long sleep — after 30 s dark the carousel returns to Speed (issue #159),
// done while the panel is still off so the wake never shows the old tile.
uint32_t asleepForMs(uint32_t nowMs);

}  // namespace sleep_policy
