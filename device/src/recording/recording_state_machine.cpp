#include "recording/recording_state_machine.h"

bool RecordingStateMachine::isValidTransition(RecordingState current, RecordingState next) {
  switch (current) {
    case RecordingState::IDLE:
      return next == RecordingState::STARTING;
    case RecordingState::STARTING:
      return next == RecordingState::ACTIVE || next == RecordingState::FAILED;
    case RecordingState::ACTIVE:
      // FAILED: a write failed mid-recording (e.g. filesystem full).
      return next == RecordingState::STOPPING || next == RecordingState::FAILED;
    case RecordingState::STOPPING:
      // FAILED: the final flush during shutdown failed.
      return next == RecordingState::IDLE || next == RecordingState::FAILED;
    case RecordingState::FAILED:
      return next == RecordingState::IDLE;
  }
  return false;
}

RecordingStateMachine::ToggleAction RecordingStateMachine::toggleAction(RecordingState current) {
  switch (current) {
    case RecordingState::IDLE:
      return ToggleAction::START;
    case RecordingState::ACTIVE:
      return ToggleAction::STOP;
    case RecordingState::FAILED:
      return ToggleAction::RESET;
    case RecordingState::STARTING:
    case RecordingState::STOPPING:
      return ToggleAction::NONE;
  }
  return ToggleAction::NONE;
}

RecordingStateMachine::ToggleAction RecordingStateMachine::setOnAction(RecordingState current) {
  switch (current) {
    case RecordingState::IDLE:
      return ToggleAction::START;
    case RecordingState::STARTING:
    case RecordingState::ACTIVE:
      // Already on (or heading there) — idempotent no-op.
      return ToggleAction::NONE;
    case RecordingState::STOPPING:
      // Transition in progress; the sender's 1/s retry will land once IDLE.
      return ToggleAction::NONE;
    case RecordingState::FAILED:
      // Don't silently clear a fault on a retried "start"; a set-off (or a
      // local toggle) resets FAILED first.
      return ToggleAction::NONE;
  }
  return ToggleAction::NONE;
}

RecordingStateMachine::ToggleAction RecordingStateMachine::setOffAction(RecordingState current) {
  switch (current) {
    case RecordingState::ACTIVE:
      return ToggleAction::STOP;
    case RecordingState::FAILED:
      // "Make it not recording" normalises a fault back to IDLE.
      return ToggleAction::RESET;
    case RecordingState::IDLE:
    case RecordingState::STOPPING:
      // Already off (or heading there) — idempotent no-op.
      return ToggleAction::NONE;
    case RecordingState::STARTING:
      // Transition in progress; retries apply once it reaches ACTIVE.
      return ToggleAction::NONE;
  }
  return ToggleAction::NONE;
}
