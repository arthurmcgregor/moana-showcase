#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "event_queue.h"

using event_queue::kMaxEvents;
using event_queue::kPersistHeaderBytes;
using event_queue::kResendIntervalMs;
using event_queue::Queue;
using event_queue::Store;

namespace {

// In-memory Store fake — the port's test adapter (production is NVS).
class FakeStore : public Store {
 public:
  size_t load(uint8_t* buf, size_t cap) override {
    if (blob.empty() || blob.size() > cap) return 0;
    memcpy(buf, blob.data(), blob.size());
    return blob.size();
  }
  bool save(const uint8_t* data, size_t len) override {
    saves++;
    blob.assign(data, data + len);
    return true;
  }

  std::vector<uint8_t> blob;
  int saves = 0;
};

// A store that cannot commit — a full or failing NVS partition. FakeStore
// always succeeds, so nothing else in this file reaches that branch.
class FailingStore : public Store {
 public:
  size_t load(uint8_t* buf, size_t cap) override {
    if (blob.empty() || blob.size() > cap) return 0;
    memcpy(buf, blob.data(), blob.size());
    return blob.size();
  }
  bool save(const uint8_t*, size_t) override {
    failedSaves++;
    return false;
  }

  std::vector<uint8_t> blob;
  int failedSaves = 0;
};

// Every event the watch queues today is a mode change (issue #146: one wire
// kind, the mode travels in its own field) — this keeps the call sites about
// the mode, which is all these tests care about.
uint16_t pushMode(Queue& q, uint8_t mode, uint32_t utcSeconds,
                  uint32_t nowMs) {
  return q.push(moana::EVENT_MODE_CHANGE, mode, utcSeconds, nowMs);
}

// --- Seq assignment ---

TEST(EventQueue, AssignsSequentialSeqsStartingAtOne) {
  FakeStore store;
  Queue q(store);
  EXPECT_EQ(pushMode(q, moana::MODE_SAIL, 100, 0), 1);
  EXPECT_EQ(pushMode(q, moana::MODE_MOTOR, 200, 0), 2);
  EXPECT_EQ(q.pendingCount(), 2u);
  EXPECT_EQ(q.pendingAt(0).kind, moana::EVENT_MODE_CHANGE);
  EXPECT_EQ(q.pendingAt(0).mode, moana::MODE_SAIL);
  EXPECT_EQ(q.pendingAt(0).utc_seconds, 100u);
}

// --- Send cadence ---

TEST(EventQueue, EmptyQueueNeverSends) {
  FakeStore store;
  Queue q(store);
  EXPECT_FALSE(q.tick(0, moana::kEventSeqNone, false));
  EXPECT_FALSE(q.tick(5000, moana::kEventSeqNone, false));
}

TEST(EventQueue, SendsImmediatelyAfterPushThenOncePerSecond) {
  FakeStore store;
  Queue q(store);
  pushMode(q, moana::MODE_SAIL, 100, 1000);

  EXPECT_TRUE(q.tick(1000, moana::kEventSeqNone, false));   // armed by push
  EXPECT_FALSE(q.tick(1250, moana::kEventSeqNone, false));  // within 1 s
  EXPECT_FALSE(q.tick(1750, moana::kEventSeqNone, false));
  EXPECT_TRUE(q.tick(2000, moana::kEventSeqNone, false));  // 1 s cadence
  EXPECT_FALSE(q.tick(2250, moana::kEventSeqNone, false));
}

TEST(EventQueue, ResendsAllUnackedWithoutLink) {
  // ackValid=false (never any telemetry): nothing trims, everything resends.
  FakeStore store;
  Queue q(store);
  pushMode(q, moana::MODE_SAIL, 100, 0);
  pushMode(q, moana::MODE_OTHER, 200, 0);
  EXPECT_TRUE(q.tick(0, moana::kEventSeqNone, false));
  EXPECT_EQ(q.pendingCount(), 2u);
}

// --- Ack trimming ---

TEST(EventQueue, TrimsAckedPrefixOnFrontierAdvance) {
  FakeStore store;
  Queue q(store);
  pushMode(q, moana::MODE_SAIL, 100, 0);   // seq 1
  pushMode(q, moana::MODE_MOTOR, 200, 0);  // seq 2
  pushMode(q, moana::MODE_OTHER, 300, 0);  // seq 3

  q.tick(0, 2, true);  // boat acked through seq 2
  ASSERT_EQ(q.pendingCount(), 1u);
  EXPECT_EQ(q.pendingAt(0).seq, 3);

  q.tick(1000, 3, true);
  EXPECT_EQ(q.pendingCount(), 0u);
  EXPECT_FALSE(q.tick(2000, 3, true));  // nothing left to send
}

TEST(EventQueue, StaleFrontierDoesNotUnTrim) {
  // A re-delivered old telemetry packet carries an older frontier; it must
  // simply ack less, never resurrect anything.
  FakeStore store;
  Queue q(store);
  pushMode(q, moana::MODE_SAIL, 100, 0);   // seq 1
  pushMode(q, moana::MODE_MOTOR, 200, 0);  // seq 2
  q.tick(0, 2, true);
  ASSERT_EQ(q.pendingCount(), 0u);
  pushMode(q, moana::MODE_OTHER, 300, 0);  // seq 3
  q.tick(1000, 1, true);                    // stale frontier
  ASSERT_EQ(q.pendingCount(), 1u);
  EXPECT_EQ(q.pendingAt(0).seq, 3);
}

TEST(EventQueue, NoneFrontierAcksNothing) {
  FakeStore store;
  Queue q(store);
  pushMode(q, moana::MODE_SAIL, 100, 0);
  q.tick(0, moana::kEventSeqNone, true);  // boat rebooted, no events yet
  EXPECT_EQ(q.pendingCount(), 1u);
}

// --- Persistence across reboot ---

TEST(EventQueue, QueueAndNextSeqSurviveReboot) {
  FakeStore store;
  {
    Queue q(store);
    pushMode(q, moana::MODE_SAIL, 100, 0);   // seq 1
    pushMode(q, moana::MODE_MOTOR, 200, 0);  // seq 2
    q.tick(0, 1, true);                       // seq 1 acked + trimmed
  }

  Queue q2(store);  // "reboot": restore from the same store
  ASSERT_EQ(q2.pendingCount(), 1u);
  EXPECT_EQ(q2.pendingAt(0).seq, 2);
  EXPECT_EQ(q2.pendingAt(0).kind, moana::EVENT_MODE_CHANGE);
  EXPECT_EQ(q2.pendingAt(0).mode, moana::MODE_MOTOR);
  EXPECT_EQ(q2.pendingAt(0).utc_seconds, 200u);

  // Seqs are never reused: the next push continues after the highest ever
  // assigned, even though seq 1 has been trimmed.
  EXPECT_EQ(pushMode(q2, moana::MODE_OTHER, 300, 0), 3);

  // Restored events are armed for immediate delivery.
  EXPECT_TRUE(q2.tick(0, moana::kEventSeqNone, false));
}

TEST(EventQueue, EveryModeRoundTripsThroughPersistence) {
  // The picker can queue any of the ten server modes (issue #146) — all of
  // them must survive the NVS blob, not just the three the buttons send.
  const uint8_t kAll[] = {
      moana::MODE_SAIL,      moana::MODE_MOTORSAIL, moana::MODE_MOTOR,
      moana::MODE_ROW,       moana::MODE_UNPOWERED, moana::MODE_ANCHOR,
      moana::MODE_ALONGSIDE, moana::MODE_BEACHED,   moana::MODE_TRAILERED,
      moana::MODE_OTHER,
  };
  const size_t n = sizeof(kAll) / sizeof(kAll[0]);

  FakeStore store;
  {
    Queue q(store);
    for (size_t i = 0; i < n; i++) {
      pushMode(q, kAll[i], 1000 + i, 0);
    }
  }

  Queue q2(store);
  ASSERT_EQ(q2.pendingCount(), n);
  for (size_t i = 0; i < n; i++) {
    EXPECT_EQ(q2.pendingAt(i).kind, moana::EVENT_MODE_CHANGE);
    EXPECT_EQ(q2.pendingAt(i).mode, kAll[i]);
    EXPECT_EQ(q2.pendingAt(i).utc_seconds, 1000u + i);
  }
}

TEST(EventQueue, EmptyOrCorruptStoreStartsFresh) {
  FakeStore store;
  store.blob = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};  // garbage
  Queue q(store);
  EXPECT_EQ(q.pendingCount(), 0u);
  EXPECT_EQ(pushMode(q, moana::MODE_SAIL, 100, 0), 1);
}

// --- Overflow ---

TEST(EventQueue, OldestUnackedSeqIsTheHeadOfTheQueue) {
  // Stamped into every Event packet (issue #177) so the boat can tell a gap
  // that will fill from one that never can.
  FakeStore store;
  Queue q(store);
  EXPECT_EQ(q.oldestUnackedSeq(), moana::kEventSeqNone);  // nothing to send

  pushMode(q, moana::MODE_SAIL, 100, 0);
  pushMode(q, moana::MODE_MOTOR, 200, 0);
  EXPECT_EQ(q.oldestUnackedSeq(), 1);

  q.tick(0, 1, true);  // the boat acked seq 1
  EXPECT_EQ(q.oldestUnackedSeq(), 2);

  q.tick(1000, 2, true);  // and now seq 2 — nothing left to declare
  EXPECT_EQ(q.oldestUnackedSeq(), moana::kEventSeqNone);
}

TEST(EventQueue, DropsOldestWhenFull) {
  FakeStore store;
  Queue q(store);
  for (size_t i = 0; i < kMaxEvents; i++) {
    pushMode(q, moana::MODE_SAIL, i, 0);
  }
  ASSERT_EQ(q.pendingCount(), kMaxEvents);
  ASSERT_EQ(q.pendingAt(0).seq, 1);

  uint16_t seq = pushMode(q, moana::MODE_OTHER, 9999, 0);
  EXPECT_EQ(q.pendingCount(), kMaxEvents);
  EXPECT_EQ(q.pendingAt(0).seq, 2);  // oldest dropped
  EXPECT_EQ(q.pendingAt(kMaxEvents - 1).seq, seq);
}

// --- Shared contiguous-ack helpers (moana::eventSeq*) ---
// The one shared implementation of the window arithmetic lives in
// shared/espnow_protocol.h; exercise its edges here.

TEST(EventSeqHelpers, NextSeqSkipsReservedZeroOnWrap) {
  EXPECT_EQ(moana::nextEventSeq(1), 2);
  EXPECT_EQ(moana::nextEventSeq(0xFFFF), 1);  // wraps past kEventSeqNone
  EXPECT_EQ(moana::nextEventSeq(moana::kEventSeqNone), 1);
}

TEST(EventSeqHelpers, AckCoverageBasics) {
  EXPECT_TRUE(moana::eventSeqAcked(1, 1));
  EXPECT_TRUE(moana::eventSeqAcked(1, 5));
  EXPECT_FALSE(moana::eventSeqAcked(6, 5));
  // The none-frontier acks nothing, and seq 0 is never a real event.
  EXPECT_FALSE(moana::eventSeqAcked(1, moana::kEventSeqNone));
  EXPECT_FALSE(moana::eventSeqAcked(moana::kEventSeqNone, 5));
}

TEST(EventSeqHelpers, AckCoverageAcrossWraparound) {
  // Frontier just past the wrap: recent pre-wrap seqs are acked, the next
  // few post-wrap seqs are not.
  EXPECT_TRUE(moana::eventSeqAcked(0xFFFE, 2));
  EXPECT_TRUE(moana::eventSeqAcked(0xFFFF, 2));
  EXPECT_TRUE(moana::eventSeqAcked(2, 2));
  EXPECT_FALSE(moana::eventSeqAcked(3, 2));
}

TEST(EventSeqHelpers, IsNextAcrossWrapAndFromNone) {
  EXPECT_TRUE(moana::eventSeqIsNext(1, moana::kEventSeqNone));
  EXPECT_FALSE(moana::eventSeqIsNext(2, moana::kEventSeqNone));
  EXPECT_TRUE(moana::eventSeqIsNext(6, 5));
  EXPECT_TRUE(moana::eventSeqIsNext(1, 0xFFFF));  // wrap skips seq 0
}

TEST(EventSeqHelpers, PrevIsTheExactInverseOfNext) {
  // The boat sets a frontier to prevEventSeq(head) and then expects head to
  // be next — so a round trip that lands anywhere else would have it ask for
  // a seq no watch will ever send.
  EXPECT_EQ(moana::prevEventSeq(2), 1);
  EXPECT_EQ(moana::prevEventSeq(1), 0xFFFF);  // wrap skips the reserved 0
  uint16_t seq = 1;
  for (uint32_t i = 0; i < 65535; i++) {
    ASSERT_EQ(moana::nextEventSeq(moana::prevEventSeq(seq)), seq);
    seq = moana::nextEventSeq(seq);
  }
}

TEST(EventSeqHelpers, PermanentGapNeedsAHeadPastWhatWeAreWaitingFor) {
  // The watch's head is the head of its queue; anything below it is acked or
  // dropped, so a frontier waiting under it waits forever (issue #177).
  EXPECT_TRUE(moana::eventSeqGapIsPermanent(3, 1, 3));   // 2 is gone
  EXPECT_TRUE(moana::eventSeqGapIsPermanent(7, moana::kEventSeqNone, 7));

  // Not a gap at all: the head is exactly what we expect next, or is behind
  // a frontier that already covers it.
  EXPECT_FALSE(moana::eventSeqGapIsPermanent(2, 1, 3));
  EXPECT_FALSE(moana::eventSeqGapIsPermanent(1, 1, 3));
  EXPECT_FALSE(moana::eventSeqGapIsPermanent(1, moana::kEventSeqNone, 1));

  // Not believable: silence, or a head ahead of the packet carrying it.
  EXPECT_FALSE(moana::eventSeqGapIsPermanent(moana::kEventSeqNone, 1, 3));
  EXPECT_FALSE(moana::eventSeqGapIsPermanent(500, 1, 3));
}

// --- Shared sailing-mode vocabulary (issue #146) ---

TEST(SailingModeHelpers, OnlyRealModesAreSelectable) {
  // UNKNOWN is an echo state ("nothing applied since boot"), never a pick,
  // and anything past OTHER is a value only a newer firmware knows.
  EXPECT_FALSE(moana::isSelectableSailingMode(moana::MODE_UNKNOWN));
  EXPECT_TRUE(moana::isSelectableSailingMode(moana::MODE_SAIL));
  EXPECT_TRUE(moana::isSelectableSailingMode(moana::MODE_ANCHOR));
  EXPECT_TRUE(moana::isSelectableSailingMode(moana::MODE_OTHER));
  EXPECT_FALSE(moana::isSelectableSailingMode(moana::MODE_OTHER + 1));
  EXPECT_FALSE(moana::isSelectableSailingMode(255));
}

TEST(SailingModeHelpers, EventSelectsModeOnlyForModeChanges) {
  EXPECT_EQ(moana::sailingModeForEvent(moana::EVENT_MODE_CHANGE,
                                       moana::MODE_ANCHOR),
            moana::MODE_ANCHOR);
  // A non-mode kind selects nothing even with a mode byte set, and an
  // unknown mode value must not be applied blindly.
  EXPECT_EQ(moana::sailingModeForEvent(moana::EVENT_VOICE_MARKER,
                                       moana::MODE_ANCHOR),
            moana::MODE_UNKNOWN);
  EXPECT_EQ(moana::sailingModeForEvent(moana::EVENT_MODE_CHANGE, 99),
            moana::MODE_UNKNOWN);
}

TEST(SailingModeHelpers, NamesAreTheServerModeChoices) {
  // These exact strings are the sidecar vocabulary the server parses back
  // (server/moana/tracks/events_jsonl.py MODE_KINDS).
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_SAIL), "SAIL");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_MOTORSAIL), "MOTORSAIL");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_MOTOR), "MOTOR");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_ROW), "ROW");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_UNPOWERED), "UNPOWERED");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_ANCHOR), "ANCHOR");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_ALONGSIDE), "ALONGSIDE");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_BEACHED), "BEACHED");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_TRAILERED), "TRAILERED");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_OTHER), "OTHER");
  EXPECT_STREQ(moana::sailingModeName(moana::MODE_UNKNOWN), "UNKNOWN");
}

// --- Store failures ---

TEST(EventQueue, PersistFailureIsSilentAndCostsTheEventOnReboot) {
  // Current behaviour, pinned deliberately: persist() discards what
  // Store::save returns, so a full NVS partition changes nothing the watch
  // can see. While it stays up the event is queued and delivered from RAM
  // exactly as normal — it is the reboot that loses it, along with the
  // never-reused seq counter. Filed in the Phase 2 findings on #152.
  FailingStore store;
  Queue q(store);

  EXPECT_EQ(pushMode(q, moana::MODE_SAIL, 100, 0), 1);
  EXPECT_EQ(q.pendingCount(), 1u);
  EXPECT_TRUE(q.tick(0, moana::kEventSeqNone, false));  // still sent
  EXPECT_GT(store.failedSaves, 0);

  Queue q2(store);  // reboot
  EXPECT_EQ(q2.pendingCount(), 0u);
  EXPECT_EQ(pushMode(q2, moana::MODE_MOTOR, 200, 0), 1);  // seq 1 reused
}

// --- Restore rejection ---

TEST(EventQueue, RestoreRejectsATruncatedBlob) {
  // The NVS adapter commits atomically, but a blob shorter than its own
  // header claims must not be replayed as far as it goes.
  FakeStore store;
  {
    Queue q(store);
    pushMode(q, moana::MODE_SAIL, 100, 0);
    pushMode(q, moana::MODE_MOTOR, 200, 0);
  }
  ASSERT_GT(store.blob.size(), kPersistHeaderBytes);
  store.blob.pop_back();  // last record one byte short

  Queue q2(store);
  EXPECT_EQ(q2.pendingCount(), 0u);
}

TEST(EventQueue, RestoreRejectsAnImpossibleCount) {
  FakeStore store;
  {
    Queue q(store);
    pushMode(q, moana::MODE_SAIL, 100, 0);
  }
  store.blob[6] = (uint8_t)(kMaxEvents + 1);  // count field, little-endian
  store.blob[7] = 0;

  Queue q2(store);
  EXPECT_EQ(q2.pendingCount(), 0u);
}

TEST(EventQueue, RestoreRejectsAReservedNextSeq) {
  // nextSeq == kEventSeqNone would hand out seq 0, which the protocol
  // reserves for "no events ever received".
  FakeStore store;
  {
    Queue q(store);
    pushMode(q, moana::MODE_SAIL, 100, 0);
  }
  store.blob[4] = 0;  // nextSeq field
  store.blob[5] = 0;

  Queue q2(store);
  EXPECT_EQ(q2.pendingCount(), 0u);
  EXPECT_EQ(pushMode(q2, moana::MODE_SAIL, 100, 0), 1);
}

// --- Seq exhaustion ---

TEST(EventQueue, SeqWrapsPastTheReservedZeroAndKeepsTrimming) {
  // Seqs are uint16 and documented as never reused. Push past the ceiling
  // with the boat acking each one, and check no event is ever assigned the
  // reserved seq and that trimming still works either side of the wrap.
  FakeStore store;
  Queue q(store);
  bool wrapped = false;

  for (uint32_t i = 0; i < 70000; i++) {
    uint16_t seq = pushMode(q, moana::MODE_SAIL, i, 0);
    ASSERT_NE(seq, moana::kEventSeqNone) << "reserved seq assigned at i=" << i;
    q.tick(i, seq, true);
    ASSERT_EQ(q.pendingCount(), 0u) << "not trimmed at seq " << seq;
    if (i > 0 && seq == 1) {
      wrapped = true;
    }
  }

  EXPECT_TRUE(wrapped) << "70000 pushes should have crossed the uint16 ceiling";
}

// --- Frontiers the boat should not be able to send ---

TEST(EventQueue, AFrontierAheadOfEverythingSentTrimsTheWholeQueue) {
  // The queue trusts the echoed frontier absolutely — it has no way to tell a
  // real cumulative ack from one left over from a previous watch generation
  // (event_ingest.h calls that the NVS-erase case). Pinned so that changing
  // it is a decision rather than an accident. Phase 2 findings on #152.
  FakeStore store;
  Queue q(store);
  pushMode(q, moana::MODE_SAIL, 100, 0);
  pushMode(q, moana::MODE_MOTOR, 200, 0);
  pushMode(q, moana::MODE_OTHER, 300, 0);

  q.tick(0, 500, true);  // a frontier no event of ours could have reached

  EXPECT_EQ(q.pendingCount(), 0u);
}

TEST(EventQueue, OverflowDropsAnUnackedEventAndDeclaresTheHole) {
  // Drop-oldest is right for the user — the newest press is the one they just
  // made — and it can drop an event the boat has NOT acked. That event is
  // gone, but delivery is not: the queue's declared head steps past it, which
  // is what the boat needs to unfreeze its contiguous frontier (issue #177,
  // EventIngest.ALiveFrontierStepsOverAnEventTheWatchHasDropped). Before that
  // field the frontier stuck at 1 for the rest of the session and this queue
  // could never be trimmed by an ack again.
  FakeStore store;
  Queue q(store);
  for (size_t i = 0; i < kMaxEvents; i++) {
    pushMode(q, moana::MODE_SAIL, i, 0);  // seqs 1..64
  }
  q.tick(0, 1, true);  // the boat acked seq 1 and nothing since
  ASSERT_EQ(q.pendingCount(), kMaxEvents - 1);
  ASSERT_EQ(q.oldestUnackedSeq(), 2);

  pushMode(q, moana::MODE_MOTOR, 1000, 0);  // seq 65 — full again
  ASSERT_EQ(q.pendingCount(), kMaxEvents);
  pushMode(q, moana::MODE_ANCHOR, 1001, 0);  // seq 66 — drops unacked seq 2

  EXPECT_EQ(q.pendingAt(0).seq, 3);
  EXPECT_EQ(q.oldestUnackedSeq(), 3);  // the next burst says seq 2 is gone

  // The boat steps its frontier over the hole and acks 3; the queue trims.
  q.tick(1000, 3, true);
  EXPECT_EQ(q.pendingCount(), kMaxEvents - 1);
  EXPECT_EQ(q.oldestUnackedSeq(), 4);
}

}  // namespace
