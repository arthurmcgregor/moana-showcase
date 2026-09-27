#pragma once

#include <stddef.h>
#include <stdint.h>

// GPIO0 button input + DRV2605 haptics (issue #139), ported from the
// watch-scoping-firmware branch's proven classifier (debounce 30 ms,
// double-click gap 300 ms, long press 600 ms).
//
// Detection only lives here — what a press *means* (mode toggling, event
// queueing, screen wake) is wired by ui.cpp through the callbacks, keeping
// this module free of model/queue knowledge.
//
// poll() must be called from loop() (every ~5 ms): the 250 ms UI tick is far
// too coarse for the 30 ms debounce and 300 ms double-click discrimination.
// Callbacks therefore run on the loop task, same task as LVGL — safe to
// touch LVGL state from them.
namespace inputs {

// DRV2605 effect library IDs — one distinguishable pattern per action so
// mode changes can be confirmed without looking at the screen.
constexpr uint8_t kHapticSail = 1;    // strong click
constexpr uint8_t kHapticMotor = 10;  // double click
constexpr uint8_t kHapticOther = 47;  // buzz
constexpr uint8_t kHapticLegCut = 24; // sharp tick — manual leg cut (#142)
// Mode picked from the on-screen picker (#146). Deliberately the same strong
// click as kHapticSail rather than a per-mode pattern: ten modes have no
// memorable ten-way vocabulary, and a picker tap is eyes-ON anyway — it needs
// to confirm that the tap registered, not to identify which row was hit.
constexpr uint8_t kHapticModePick = 1;

// A pause between effects in a sequence, as the DRV2605 encodes it in a
// waveform slot: bit 7 marks the slot a wait, the low bits count 10 ms
// units. Capped at the 1270 ms the register can hold.
constexpr uint8_t hapticWaitMs(uint16_t ms) {
  return (uint8_t)(0x80 | (ms / 10 > 127 ? 127 : ms / 10));
}

// Fault alerts (issue #162) are sequences, not single effects. A mode haptic
// only has to confirm a press you just made, so one click carries it; an
// alert has to interrupt someone who is sailing the boat and not thinking
// about the watch at all, and a single one-second effect is easy to miss
// under a wet sleeve. Each of these fills the DRV2605's sequencer with
// repeated full-strength effects, so the buzz lasts ~3 s.
//
// The two faults are told apart by texture rather than by counting pulses:
// the link alert is the library's smooth alert effect, the fix alert its
// rough buzz. Same length, same strength, unmistakably not each other.
constexpr uint8_t kAlertLinkLost[] = {16, hapticWaitMs(200),   // 1000 ms alert
                                      16, hapticWaitMs(200),  //
                                      16};
constexpr uint8_t kAlertGpsLost[] = {47, hapticWaitMs(200),   // buzz 1, 100%
                                     47, hapticWaitMs(200),  //
                                     47};
// Fault cleared: two light clicks. Deliberately nothing like the alerts —
// good news should read as good news without being looked at.
constexpr uint8_t kAlertCleared[] = {1, hapticWaitMs(100), 1};

struct Callbacks {
  void (*onSinglePress)();
  void (*onDoublePress)();
  void (*onLongPress)();
  // PWR crown click (issue #142: manual leg cut). Delivered through the
  // AXP2101 PKEY IRQ -> LilyGoLib POWER_EVENT, dispatched by instance.loop()
  // on the loop task — same task as LVGL, safe to touch UI state.
  void (*onPwrClick)();
};

void begin(const Callbacks& callbacks);
void poll();

// Play one DRV2605 library effect (fire-and-forget).
void haptic(uint8_t effect);

// Play a sequence of up to kHapticSlots effects and waits (fire-and-forget:
// the DRV2605's sequencer runs it without the CPU). Anything past the eighth
// slot is dropped — the chip has no ninth.
constexpr size_t kHapticSlots = 8;
void hapticSlots(const uint8_t* slots, size_t count);

template <size_t N>
inline void haptic(const uint8_t (&slots)[N]) {
  hapticSlots(slots, N);
}

}  // namespace inputs
