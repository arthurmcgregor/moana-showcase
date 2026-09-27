#include <gtest/gtest.h>

#include "command_channel.h"

using command_channel::Channel;
using command_channel::kResendIntervalMs;
using command_channel::kTimeoutMs;

namespace {

constexpr uint8_t kIdle = moana::RECORDING_IDLE;
constexpr uint8_t kStarting = moana::RECORDING_STARTING;
constexpr uint8_t kActive = moana::RECORDING_ACTIVE;
constexpr uint8_t kStopping = moana::RECORDING_STOPPING;
constexpr uint8_t kFailed = moana::RECORDING_FAILED;

// --- Happy path: send -> echo confirms ---

TEST(CommandChannel, RequestSendsImmediatelyOnNextTick) {
  Channel ch;
  ASSERT_TRUE(ch.request(true, kIdle, true, 1000));
  EXPECT_TRUE(ch.pending());

  Channel::Tick t = ch.tick(1000, kIdle, true);
  EXPECT_TRUE(t.send);
  EXPECT_TRUE(t.desiredOn);
}

TEST(CommandChannel, EchoConfirmationStopsResending) {
  Channel ch;
  ch.request(true, kIdle, true, 0);
  EXPECT_TRUE(ch.tick(0, kIdle, true).send);

  // Boat echoes STARTING — command received, channel settles.
  EXPECT_FALSE(ch.tick(500, kStarting, true).send);
  EXPECT_FALSE(ch.pending());
  EXPECT_FALSE(ch.consumeTimeout());

  // And stays quiet afterwards.
  EXPECT_FALSE(ch.tick(2000, kActive, true).send);
}

TEST(CommandChannel, ActiveEchoAlsoConfirmsDesiredOn) {
  Channel ch;
  ch.request(true, kIdle, true, 0);
  ch.tick(0, kIdle, true);
  EXPECT_FALSE(ch.tick(900, kActive, true).send);
  EXPECT_FALSE(ch.pending());
}

TEST(CommandChannel, StopConfirmedByStoppingOrIdleEcho) {
  Channel ch;
  ch.request(false, kActive, true, 0);
  EXPECT_TRUE(ch.tick(0, kActive, true).send);
  EXPECT_FALSE(ch.tick(800, kStopping, true).send);
  EXPECT_FALSE(ch.pending());

  Channel ch2;
  ch2.request(false, kActive, true, 0);
  ch2.tick(0, kActive, true);
  EXPECT_FALSE(ch2.tick(800, kIdle, true).send);
  EXPECT_FALSE(ch2.pending());
}

// --- Lost packet: resend 1/s while unconfirmed ---

TEST(CommandChannel, ResendsOncePerSecondWhileUnconfirmed) {
  Channel ch;
  ch.request(true, kIdle, true, 0);

  EXPECT_TRUE(ch.tick(0, kIdle, true).send);
  // 4 Hz UI ticks between resends stay quiet.
  EXPECT_FALSE(ch.tick(250, kIdle, true).send);
  EXPECT_FALSE(ch.tick(500, kIdle, true).send);
  EXPECT_FALSE(ch.tick(750, kIdle, true).send);
  // One resend interval later it fires again.
  EXPECT_TRUE(ch.tick(kResendIntervalMs, kIdle, true).send);
  EXPECT_FALSE(ch.tick(kResendIntervalMs + 250, kIdle, true).send);
  EXPECT_TRUE(ch.tick(2 * kResendIntervalMs, kIdle, true).send);
}

TEST(CommandChannel, ResendCarriesTheSameDesiredState) {
  Channel ch;
  ch.request(false, kActive, true, 0);
  Channel::Tick first = ch.tick(0, kActive, true);
  Channel::Tick second = ch.tick(kResendIntervalMs, kActive, true);
  ASSERT_TRUE(first.send);
  ASSERT_TRUE(second.send);
  EXPECT_FALSE(first.desiredOn);
  EXPECT_FALSE(second.desiredOn);
}

// --- Timeout after 10 s ---

TEST(CommandChannel, GivesUpAfterTimeoutAndReportsIt) {
  Channel ch;
  ch.request(true, kIdle, true, 0);

  uint32_t now = 0;
  int sends = 0;
  while (now < kTimeoutMs + 2000) {
    if (ch.tick(now, kIdle, true).send) {
      sends++;
    }
    now += 250;
  }

  EXPECT_FALSE(ch.pending());
  EXPECT_EQ(sends, 10);  // one initial + resends, none after the deadline
  EXPECT_TRUE(ch.consumeTimeout());
  // The latch clears on read.
  EXPECT_FALSE(ch.consumeTimeout());
}

TEST(CommandChannel, TimesOutEvenWithNoEchoAtAll) {
  // Walking out of range before the first telemetry packet: echoValid stays
  // false, and the channel still gives up after 10 s rather than never.
  Channel ch;
  ch.request(true, kIdle, false, 0);
  EXPECT_TRUE(ch.tick(0, 0, false).send);
  EXPECT_FALSE(ch.tick(kTimeoutMs, 0, false).send);
  EXPECT_FALSE(ch.pending());
  EXPECT_TRUE(ch.consumeTimeout());
}

// --- Echo changing externally (device-side start, no command) ---

TEST(CommandChannel, ExternalStateChangeCausesNoSends) {
  Channel ch;
  // Recording started on the device itself; the watch just observes the
  // echo flip. Nothing pending -> nothing to send, ever.
  EXPECT_FALSE(ch.tick(0, kIdle, true).send);
  EXPECT_FALSE(ch.tick(1000, kStarting, true).send);
  EXPECT_FALSE(ch.tick(2000, kActive, true).send);
  EXPECT_FALSE(ch.pending());
  EXPECT_FALSE(ch.consumeTimeout());
}

// --- Idempotency ---

TEST(CommandChannel, RequestingTheEchoedStateIsANoOp) {
  Channel ch;
  // Boat already recording; "start" changes nothing.
  EXPECT_FALSE(ch.request(true, kActive, true, 0));
  EXPECT_FALSE(ch.pending());
  EXPECT_FALSE(ch.tick(0, kActive, true).send);

  // Boat already idle; "stop" changes nothing.
  EXPECT_FALSE(ch.request(false, kIdle, true, 0));
  EXPECT_FALSE(ch.pending());
}

TEST(CommandChannel, RepeatedRequestDoesNotResetTheTimeoutClock) {
  Channel ch;
  ch.request(true, kIdle, true, 0);
  ch.tick(0, kIdle, true);

  // Impatient re-request halfway through the window.
  ch.request(true, kIdle, true, 5000);

  // Deadline still measured from the original request: expired at 10 s.
  EXPECT_FALSE(ch.tick(kTimeoutMs, kIdle, true).send);
  EXPECT_FALSE(ch.pending());
  EXPECT_TRUE(ch.consumeTimeout());
}

TEST(CommandChannel, OppositeRequestReplacesThePendingCommand) {
  Channel ch;
  ch.request(true, kIdle, true, 0);
  ch.tick(0, kIdle, true);

  // Changed their mind: stop instead. New command, new clock.
  ch.request(false, kIdle, true, 4000);
  Channel::Tick t = ch.tick(4000, kIdle, true);
  ASSERT_TRUE(t.send);
  EXPECT_FALSE(t.desiredOn);

  // ... which IDLE immediately confirms.
  EXPECT_FALSE(ch.tick(4250, kIdle, true).send);
  EXPECT_FALSE(ch.pending());
}

TEST(CommandChannel, FailedEchoNeverConfirmsAndTimesOut) {
  Channel ch;
  ch.request(true, kIdle, true, 0);
  ch.tick(0, kIdle, true);
  // Boat tried and failed; FAILED confirms neither on nor off.
  ch.tick(1500, kFailed, true);
  EXPECT_TRUE(ch.pending());
  uint32_t now = 2000;
  while (now <= kTimeoutMs) {
    ch.tick(now, kFailed, true);
    now += 250;
  }
  EXPECT_FALSE(ch.pending());
  EXPECT_TRUE(ch.consumeTimeout());
}

}  // namespace
