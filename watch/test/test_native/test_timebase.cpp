#include <gtest/gtest.h>

#include "timebase.h"

namespace {

// Stands in for the PCF85063: records what was written, hands back whatever
// it is told to hold. `held = 0` is the hardware's "nothing real in here",
// which the real adapter reports off the oscillator-stop bit.
class FakeClock : public timebase::Clock {
 public:
  uint32_t held = 0;
  int reads = 0;
  int writes = 0;
  uint32_t lastWritten = 0;
  // The real adapter reads the registers back to answer this, because the
  // driver's setDateTime cannot (issue #144). False = the write did not land.
  bool writeSucceeds = true;

  uint32_t readUtc() override {
    reads++;
    return held;
  }
  bool writeUtc(uint32_t utcSeconds) override {
    writes++;
    lastWritten = utcSeconds;
    if (!writeSucceeds) {
      return false;
    }
    held = utcSeconds;
    return true;
  }
};

// A plausible UTC: 2026-09-04T00:00:00Z, comfortably past kPlausibleEpoch.
constexpr uint32_t kSaneUtc = 1788480000;

class TimebaseTest : public ::testing::Test {
 protected:
  void SetUp() override { timebase::reset(); }
};

TEST_F(TimebaseTest, ReturnsZeroBeforeAnyReference) {
  // Protocol sentinel: 0 = time unknown. Events stamped before the first
  // telemetry UTC must carry it.
  EXPECT_EQ(timebase::now(123456), 0u);
}

TEST_F(TimebaseTest, ExtrapolatesFromReferenceByMillisDelta) {
  timebase::feed(1'753'900'000, 10'000);
  EXPECT_EQ(timebase::now(10'000), 1'753'900'000u);
  EXPECT_EQ(timebase::now(10'999), 1'753'900'000u);  // sub-second truncated
  EXPECT_EQ(timebase::now(15'000), 1'753'900'005u);
}

TEST_F(TimebaseTest, IgnoresZeroUtcReference) {
  timebase::feed(0, 10'000);  // sender had no GPS time
  EXPECT_EQ(timebase::now(20'000), 0u);
}

TEST_F(TimebaseTest, FreshReferenceRebasesTheClock) {
  timebase::feed(1'000'000'000, 0);
  timebase::feed(1'000'000'100, 60'000);  // later packet corrects drift
  EXPECT_EQ(timebase::now(61'000), 1'000'000'101u);
}

TEST_F(TimebaseTest, SurvivesMillisWrapBetweenReferenceAndRead) {
  // refMs near the top of uint32; nowMs after the wrap. Unsigned subtraction
  // keeps the delta right.
  timebase::feed(2'000'000'000, 0xFFFFF000u);
  uint32_t nowMs = 0x00000FFCu;  // 8188 ms later, wrapped
  EXPECT_EQ(timebase::now(nowMs), 2'000'000'008u);
}

// --- hardware clock port (issue #144) ---

TEST_F(TimebaseTest, AdoptsAPlausibleTimeFromTheRtcAtBoot) {
  // The whole point of #144: rebooted at anchor with the boat unit off, the
  // watch still knows what time it is, and a mode press still carries a UTC
  // the server will accept.
  FakeClock clock;
  clock.held = kSaneUtc;
  timebase::begin(&clock, 5'000);
  EXPECT_EQ(timebase::now(5'000), kSaneUtc);
  EXPECT_EQ(timebase::now(65'000), kSaneUtc + 60);
  EXPECT_EQ(clock.reads, 1);  // read once at boot, never on the tick
}

TEST_F(TimebaseTest, StaysUnknownWhenTheRtcHoldsNothingReal) {
  FakeClock clock;  // held = 0: oscillator stopped since it was last set
  timebase::begin(&clock, 5'000);
  EXPECT_EQ(timebase::now(5'000), 0u);
}

TEST_F(TimebaseTest, RejectsAnImplausiblyOldRtcRatherThanShowingIt) {
  // A factory-fresh PCF85063 reads out around 2000-01-01 and can have its
  // integrity bit clear. `--:--` is honest; 2000 is not.
  FakeClock clock;
  clock.held = timebase::kPlausibleEpoch - 1;
  timebase::begin(&clock, 5'000);
  EXPECT_EQ(timebase::now(5'000), 0u);
}

TEST_F(TimebaseTest, DisciplinesTheRtcOnTheFirstRealReference) {
  FakeClock clock;
  timebase::begin(&clock, 0);  // nothing held
  timebase::feed(kSaneUtc, 1'000);
  EXPECT_EQ(clock.writes, 1);
  EXPECT_EQ(clock.lastWritten, kSaneUtc);
}

TEST_F(TimebaseTest, DoesNotTouchTheRtcWhileTheClockIsAlreadyRight) {
  // 1 Hz telemetry against a bus shared with the PMU, touch controller and
  // haptic driver: a write per packet is the thing to avoid.
  FakeClock clock;
  clock.held = kSaneUtc;
  timebase::begin(&clock, 0);
  for (uint32_t i = 1; i <= 60; i++) {
    timebase::feed(kSaneUtc + i, i * 1'000);
  }
  EXPECT_EQ(clock.writes, 0);
}

TEST_F(TimebaseTest, ToleratesTheOneSecondTheTwoWholeSecondClocksDisagreeBy) {
  // now() truncates and telemetry UTC is a whole second, so a correct clock
  // still reads one out about half the time. Writing on that would be the
  // per-packet write in disguise.
  FakeClock clock;
  clock.held = kSaneUtc;
  timebase::begin(&clock, 0);
  timebase::feed(kSaneUtc + 1, 500);
  EXPECT_EQ(clock.writes, 0);
}

TEST_F(TimebaseTest, RewritesTheRtcOnceItHasActuallyDrifted) {
  FakeClock clock;
  clock.held = kSaneUtc;
  timebase::begin(&clock, 0);
  timebase::feed(kSaneUtc + 30, 1'000);  // crystal drift over a long boat-off
  EXPECT_EQ(clock.writes, 1);
  EXPECT_EQ(clock.lastWritten, kSaneUtc + 30);
  EXPECT_EQ(timebase::now(1'000), kSaneUtc + 30);
}

TEST_F(TimebaseTest, RebasesTheRamReferenceEvenWhenItSkipsTheWrite) {
  // The threshold gates the I2C write only — the in-RAM estimate always
  // follows the freshest packet, so event timestamps stay GPS-accurate.
  FakeClock clock;
  clock.held = kSaneUtc;
  timebase::begin(&clock, 0);
  timebase::feed(kSaneUtc + 1, 900);  // inside the threshold: no I2C write
  EXPECT_EQ(clock.writes, 0);
  EXPECT_EQ(timebase::now(900), kSaneUtc + 1);
  EXPECT_EQ(timebase::now(5'900), kSaneUtc + 6);  // and it extrapolates from there
}

TEST_F(TimebaseTest, RetriesAWriteTheHardwareRefused) {
  // The drift check compares telemetry against the RAM estimate, and that same
  // packet rebases the estimate — so after one write the two never disagree
  // again and drift alone would never ask for another. A write that did not
  // land has to be retried on its own account, or the wrist shows a time it
  // believes while the RTC holds nothing and the next boot reads `--:--`.
  FakeClock clock;
  clock.writeSucceeds = false;
  timebase::begin(&clock, 0);

  timebase::feed(kSaneUtc, 1'000);
  ASSERT_EQ(clock.writes, 1);
  timebase::feed(kSaneUtc + 1, 2'000);  // in step with the estimate: no drift
  EXPECT_EQ(clock.writes, 2);

  clock.writeSucceeds = true;
  timebase::feed(kSaneUtc + 2, 3'000);
  EXPECT_EQ(clock.writes, 3);
  EXPECT_EQ(clock.held, kSaneUtc + 2);

  // Taken: back to one write per drift, not one per packet.
  timebase::feed(kSaneUtc + 3, 4'000);
  EXPECT_EQ(clock.writes, 3);
}

TEST_F(TimebaseTest, AnAdoptedHardwareTimeNeedsNoWriteToConfirmIt) {
  // begin() read a plausible time out of the RTC, so the hardware is already
  // holding what this firmware thinks the time is — retrying a write it never
  // made would be a write per packet for as long as the clock stays right.
  FakeClock clock;
  clock.held = kSaneUtc;
  timebase::begin(&clock, 0);
  timebase::feed(kSaneUtc + 1, 1'000);
  timebase::feed(kSaneUtc + 2, 2'000);
  EXPECT_EQ(clock.writes, 0);
}

TEST_F(TimebaseTest, WorksWithNoClockAtAll) {
  // begin(nullptr) is the pre-#144 behaviour, and what a native test that
  // only cares about extrapolation gets.
  timebase::begin(nullptr, 0);
  EXPECT_EQ(timebase::now(1'000), 0u);
  timebase::feed(kSaneUtc, 1'000);
  EXPECT_EQ(timebase::now(2'000), kSaneUtc + 1);
}

}  // namespace
