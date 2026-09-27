#pragma once

#include "types.h"

// Pure state machine logic for recording.
// No FreeRTOS, no callbacks, no side effects — just state transitions.
namespace RecordingStateMachine {

// What action should toggle()/setRecording() trigger?
enum class ToggleAction {
  START,      // Begin recording (create task)
  STOP,       // Stop recording (signal task)
  RESET,      // Reset from FAILED to IDLE
  NONE,       // Transition in progress or already there, ignore
};

// Check if a transition from current to next is valid.
bool isValidTransition(RecordingState current, RecordingState next);

// Given the current state, what should toggle() do?
ToggleAction toggleAction(RecordingState current);

// Absolute set-on/set-off semantics for remote commands (issue #138).
// Idempotent by design: a SET to the state we are already in — or already
// transitioning towards — is NONE, not an error, so watch-side retries can
// never double-fire.
//
// setOnAction: only IDLE starts; FAILED stays FAILED (requires an explicit
// reset/set-off first, so a retried "start" can't silently clear a fault).
ToggleAction setOnAction(RecordingState current);
// setOffAction: ACTIVE stops; FAILED resets to IDLE ("make it not recording"
// normalises a fault); transitions in progress are left to finish.
ToggleAction setOffAction(RecordingState current);

}  // namespace RecordingStateMachine
