#include <gtest/gtest.h>

#include "recording/recording_state_machine.h"

using RS = RecordingState;
using TA = RecordingStateMachine::ToggleAction;

// --- Lifecycle transitions ---

TEST(RecordingStateMachine, RecordingCanBeStartedFromIdle) {
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::IDLE, RS::STARTING));
}

TEST(RecordingStateMachine, RecordingTransitionsThroughFullLifecycle) {
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::IDLE, RS::STARTING));
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::STARTING, RS::ACTIVE));
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::ACTIVE, RS::STOPPING));
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::STOPPING, RS::IDLE));
}

TEST(RecordingStateMachine, RecordingCanFailDuringStartupAndBeReset) {
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::STARTING, RS::FAILED));
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::FAILED, RS::IDLE));
}

TEST(RecordingStateMachine, RecordingCanFailMidRecordingOnWriteError) {
  // A write failure while ACTIVE (e.g. filesystem full) aborts to FAILED.
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::ACTIVE, RS::FAILED));
}

TEST(RecordingStateMachine, RecordingCanFailWhileStoppingOnFinalFlush) {
  // The final flush during shutdown can also fail.
  EXPECT_TRUE(RecordingStateMachine::isValidTransition(RS::STOPPING, RS::FAILED));
}

// --- Invalid transitions ---

TEST(RecordingStateMachine, CannotJumpDirectlyToActive) {
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::IDLE, RS::ACTIVE));
}

TEST(RecordingStateMachine, CannotSkipStoppingPhase) {
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::ACTIVE, RS::IDLE));
}

TEST(RecordingStateMachine, SelfTransitionsAreInvalid) {
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::IDLE, RS::IDLE));
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::ACTIVE, RS::ACTIVE));
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::FAILED, RS::FAILED));
}

TEST(RecordingStateMachine, CannotGoBackwardsFromStopping) {
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::STOPPING, RS::ACTIVE));
  EXPECT_FALSE(RecordingStateMachine::isValidTransition(RS::STOPPING, RS::STARTING));
}

// --- Toggle actions ---

TEST(RecordingStateMachine, ToggleFromIdleStartsRecording) {
  EXPECT_EQ(RecordingStateMachine::toggleAction(RS::IDLE), TA::START);
}

TEST(RecordingStateMachine, ToggleFromActiveStopsRecording) {
  EXPECT_EQ(RecordingStateMachine::toggleAction(RS::ACTIVE), TA::STOP);
}

TEST(RecordingStateMachine, ToggleFromFailedResetsToIdle) {
  EXPECT_EQ(RecordingStateMachine::toggleAction(RS::FAILED), TA::RESET);
}

TEST(RecordingStateMachine, ToggleDuringTransitionIsIgnored) {
  EXPECT_EQ(RecordingStateMachine::toggleAction(RS::STARTING), TA::NONE);
  EXPECT_EQ(RecordingStateMachine::toggleAction(RS::STOPPING), TA::NONE);
}

// --- Set-on actions (absolute remote command, issue #138) ---

TEST(RecordingStateMachine, SetOnFromIdleStartsRecording) {
  EXPECT_EQ(RecordingStateMachine::setOnAction(RS::IDLE), TA::START);
}

TEST(RecordingStateMachine, SetOnIsIdempotentWhenAlreadyOnOrStarting) {
  // A retried "start" while already recording must be a no-op — a toggle
  // here would stop the recording (the double-fire the SET semantics exist
  // to prevent).
  EXPECT_EQ(RecordingStateMachine::setOnAction(RS::ACTIVE), TA::NONE);
  EXPECT_EQ(RecordingStateMachine::setOnAction(RS::STARTING), TA::NONE);
}

TEST(RecordingStateMachine, SetOnDuringStoppingIsIgnored) {
  EXPECT_EQ(RecordingStateMachine::setOnAction(RS::STOPPING), TA::NONE);
}

TEST(RecordingStateMachine, SetOnFromFailedDoesNotSilentlyClearFault) {
  EXPECT_EQ(RecordingStateMachine::setOnAction(RS::FAILED), TA::NONE);
}

// --- Set-off actions ---

TEST(RecordingStateMachine, SetOffFromActiveStopsRecording) {
  EXPECT_EQ(RecordingStateMachine::setOffAction(RS::ACTIVE), TA::STOP);
}

TEST(RecordingStateMachine, SetOffIsIdempotentWhenAlreadyOffOrStopping) {
  EXPECT_EQ(RecordingStateMachine::setOffAction(RS::IDLE), TA::NONE);
  EXPECT_EQ(RecordingStateMachine::setOffAction(RS::STOPPING), TA::NONE);
}

TEST(RecordingStateMachine, SetOffDuringStartingIsIgnored) {
  EXPECT_EQ(RecordingStateMachine::setOffAction(RS::STARTING), TA::NONE);
}

TEST(RecordingStateMachine, SetOffFromFailedResetsToIdle) {
  EXPECT_EQ(RecordingStateMachine::setOffAction(RS::FAILED), TA::RESET);
}
