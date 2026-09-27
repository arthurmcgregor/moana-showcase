#include <gtest/gtest.h>

#include "events/event_ingest.h"

// Receive-side dedupe / contiguous-ack logic for watch events (issue #139).
// The seq-gap scenarios here mirror the acceptance criteria: drop event N,
// deliver N+1 -> ack holds at N-1; deliver N -> both apply, no duplicates.

namespace {

// Every event the watch sends today is a mode change (issue #146: one wire
// kind, the mode in its own field), so the seq scenarios below only vary the
// mode. Ingest treats both bytes as opaque payload — see
// EventIngest.CarriesAnyModeAndKindThrough.
//
// `oldestUnacked` is the packet's oldest_unacked_seq: the head of the sending
// watch's queue (issue #177). It is spelled out at every call because it is
// half the scenario — "the watch still holds 2 and will resend it" and "the
// watch has dropped 2 and never will" are different situations that arrive as
// byte-identical packets otherwise, and the whole point of the field is that
// the boat can now tell them apart.
moana::Event wirePacket(uint16_t seq, uint8_t kind, uint8_t mode,
                        uint32_t utcSeconds, uint16_t oldestUnacked) {
  moana::Event ev = {};
  ev.version = moana::kProtocolVersion;
  ev.type = moana::PACKET_EVENT;
  ev.seq = seq;
  ev.kind = kind;
  ev.mode = mode;
  ev.utc_seconds = utcSeconds;
  ev.oldest_unacked_seq = oldestUnacked;
  return ev;
}

std::vector<moana::Event> acceptMode(EventIngest& ingest, uint16_t seq,
                                     uint8_t mode, uint32_t utcSeconds,
                                     uint16_t oldestUnacked) {
  return ingest.accept(
      wirePacket(seq, moana::EVENT_MODE_CHANGE, mode, utcSeconds,
                 oldestUnacked));
}

TEST(EventIngest, StartsWithNoEventsAcked) {
  EventIngest ingest;
  EXPECT_EQ(ingest.ackFrontier(), moana::kEventSeqNone);
}

TEST(EventIngest, FirstEventAdvancesFrontierAndApplies) {
  EventIngest ingest;
  auto applied = acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  ASSERT_EQ(applied.size(), 1u);
  EXPECT_EQ(applied[0].mode, moana::MODE_SAIL);
  EXPECT_EQ(applied[0].utc_seconds, 1000u);
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, DuplicateAtFrontierIsIgnored) {
  EventIngest ingest;
  acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  auto applied = acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  EXPECT_TRUE(applied.empty());
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, DuplicateBehindFrontierIsIgnored) {
  EventIngest ingest;
  acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  acceptMode(ingest, 2, moana::MODE_MOTOR, 2000, 1);
  auto applied = acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  EXPECT_TRUE(applied.empty());
  EXPECT_EQ(ingest.ackFrontier(), 2);
}

TEST(EventIngest, GapHoldsFrontierUntilMissingEventArrives) {
  // Acceptance criterion: drop event N (=2), deliver N+1 (=3) -> ack holds
  // at N-1 (=1); then both are applied exactly once when N arrives. The watch
  // holds 1..3 throughout, so 2 is coming back — nothing may skip it.
  EventIngest ingest;
  acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);

  auto applied = acceptMode(ingest, 3, moana::MODE_OTHER, 3000, 1);
  EXPECT_TRUE(applied.empty());          // buffered, not applied
  EXPECT_EQ(ingest.ackFrontier(), 1);    // frontier held at the hole

  applied = acceptMode(ingest, 2, moana::MODE_MOTOR, 2000, 1);
  ASSERT_EQ(applied.size(), 2u);         // hole filled: both apply in order
  EXPECT_EQ(applied[0].mode, moana::MODE_MOTOR);
  EXPECT_EQ(applied[0].utc_seconds, 2000u);
  EXPECT_EQ(applied[1].mode, moana::MODE_OTHER);
  EXPECT_EQ(applied[1].utc_seconds, 3000u);
  EXPECT_EQ(ingest.ackFrontier(), 3);
}

TEST(EventIngest, ResentBufferedEventDoesNotDuplicate) {
  // The watch resends ALL unacked events 1/s, so a buffered out-of-order
  // event will arrive again before the hole fills. It must apply once.
  EventIngest ingest;
  acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  acceptMode(ingest, 3, moana::MODE_OTHER, 3000, 1);
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_OTHER, 3000, 1).empty());

  auto applied = acceptMode(ingest, 2, moana::MODE_MOTOR, 2000, 1);
  ASSERT_EQ(applied.size(), 2u);
  EXPECT_EQ(ingest.ackFrontier(), 3);

  // And the late replay of 3 after the frontier moved is a plain duplicate.
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_OTHER, 3000, 1).empty());
  EXPECT_EQ(ingest.ackFrontier(), 3);
}

TEST(EventIngest, MultipleBufferedSuccessorsDrainInOrder) {
  EventIngest ingest;
  acceptMode(ingest, 4, moana::MODE_MOTOR, 4000, 1);
  acceptMode(ingest, 2, moana::MODE_SAIL, 2000, 1);
  acceptMode(ingest, 3, moana::MODE_OTHER, 3000, 1);
  EXPECT_EQ(ingest.ackFrontier(), moana::kEventSeqNone);

  auto applied = acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  ASSERT_EQ(applied.size(), 4u);
  EXPECT_EQ(applied[0].utc_seconds, 1000u);
  EXPECT_EQ(applied[1].utc_seconds, 2000u);
  EXPECT_EQ(applied[2].utc_seconds, 3000u);
  EXPECT_EQ(applied[3].utc_seconds, 4000u);
  EXPECT_EQ(ingest.ackFrontier(), 4);
}

TEST(EventIngest, ReservedSeqZeroIsIgnored) {
  EventIngest ingest;
  EXPECT_TRUE(
      acceptMode(ingest, moana::kEventSeqNone, moana::MODE_SAIL, 1, 1).empty());
  EXPECT_EQ(ingest.ackFrontier(), moana::kEventSeqNone);
}

TEST(EventIngest, FrontierWrapsPastReservedZero) {
  // Walk the frontier contiguously all the way to 0xFFFF, then the next
  // event takes seq 1 (the watch skips the reserved 0) and must advance
  // the frontier across the wrap.
  EventIngest ingest;
  uint16_t seq = 1;
  for (uint32_t i = 0; i < 65535; i++) {
    ASSERT_EQ(acceptMode(ingest, seq, moana::MODE_SAIL, i, seq).size(), 1u);
    seq = moana::nextEventSeq(seq);
  }
  ASSERT_EQ(ingest.ackFrontier(), 0xFFFF);

  auto applied = acceptMode(ingest, 1, moana::MODE_MOTOR, 99, 1);
  ASSERT_EQ(applied.size(), 1u);
  EXPECT_EQ(ingest.ackFrontier(), 1);

  // A pre-wrap seq is now behind the frontier on the circle — a duplicate.
  EXPECT_TRUE(acceptMode(ingest, 0xFFFF, moana::MODE_SAIL, 1, 1).empty());
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

// --- the watch declaring what it still holds (issue #177) ---

TEST(EventIngest, RebootResyncAppliesTheBacklogOnTheFirstBurst) {
  // Boat rebooted mid-session: its frontier is back to kEventSeqNone but the
  // watch's persisted counter is far past seq 1, so every seq the watch sends
  // looks like one giant hole. The packet says the watch's queue starts at 7,
  // which settles it — 1..6 are acked-and-trimmed and will never be offered
  // again, so the frontier belongs at 6 and the backlog applies at once.
  // Before this field the boat had to wait a full resend cycle and infer it.
  EventIngest ingest;
  auto applied = acceptMode(ingest, 7, moana::MODE_SAIL, 7000, 7);
  ASSERT_EQ(applied.size(), 1u);
  EXPECT_EQ(applied[0].utc_seconds, 7000u);
  EXPECT_EQ(ingest.ackFrontier(), 7);

  ASSERT_EQ(acceptMode(ingest, 8, moana::MODE_MOTOR, 8000, 7).size(), 1u);
  ASSERT_EQ(acceptMode(ingest, 9, moana::MODE_OTHER, 9000, 7).size(), 1u);
  EXPECT_EQ(ingest.ackFrontier(), 9);
}

TEST(EventIngest, RebootResyncStillWaitsForTheHeadItselfToArrive) {
  // Same reboot, but the head's own packet is lost. The frontier still moves
  // to just below it — everything under the head is dead either way — and
  // then waits, because the watch is telling us it still holds seq 7.
  EventIngest ingest;
  EXPECT_TRUE(acceptMode(ingest, 8, moana::MODE_MOTOR, 8000, 7).empty());
  EXPECT_EQ(ingest.ackFrontier(), 6);
  EXPECT_TRUE(acceptMode(ingest, 9, moana::MODE_OTHER, 9000, 7).empty());
  EXPECT_EQ(ingest.ackFrontier(), 6);

  auto applied = acceptMode(ingest, 7, moana::MODE_SAIL, 7000, 7);
  ASSERT_EQ(applied.size(), 3u);
  EXPECT_EQ(applied[0].utc_seconds, 7000u);
  EXPECT_EQ(applied[1].utc_seconds, 8000u);
  EXPECT_EQ(applied[2].utc_seconds, 9000u);
  EXPECT_EQ(ingest.ackFrontier(), 9);
}

TEST(EventIngest, RebootResyncSinglePendingEvent) {
  // One press before the reboot: applied on the first packet that carries it,
  // rather than deadlocking forever (or, before #177, waiting for the 1/s
  // resend to prove the point).
  EventIngest ingest;
  auto applied = acceptMode(ingest, 42, moana::MODE_MOTOR, 4200, 42);
  ASSERT_EQ(applied.size(), 1u);
  EXPECT_EQ(applied[0].mode, moana::MODE_MOTOR);
  EXPECT_EQ(ingest.ackFrontier(), 42);
}

TEST(EventIngest, ALiveFrontierStepsOverAnEventTheWatchHasDropped) {
  // The watch's queue overflowed and dropped unacked seq 2 to make room
  // (EventQueue.DropsOldestWhenFull), so its head is now 3 and seq 2 will
  // never be offered by anyone again. Before #177 the frontier froze at 1 for
  // the rest of the session — the watch could never trim, resent its whole
  // queue every second forever, and only a boat reboot recovered it.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);
  ASSERT_EQ(ingest.ackFrontier(), 1);

  auto applied = acceptMode(ingest, 3, moana::MODE_MOTOR, 3000, 3);
  ASSERT_EQ(applied.size(), 1u);
  EXPECT_EQ(applied[0].seq, 3);
  EXPECT_EQ(ingest.ackFrontier(), 3);
}

TEST(EventIngest, SteppingOverADeadSeqDrainsWhatWasBufferedBehindIt) {
  // The successors buffered while seq 2 still looked live come with the jump,
  // in seq order, in the same call — so a caller that writes each applied
  // event to the sidecar sees them exactly once and in order.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_MOTOR, 3000, 2).empty());
  EXPECT_TRUE(acceptMode(ingest, 4, moana::MODE_ANCHOR, 4000, 2).empty());
  ASSERT_EQ(ingest.ackFrontier(), 1);

  // Next burst: the watch's head has moved past 2, so 2 is gone.
  auto applied = acceptMode(ingest, 5, moana::MODE_SAIL, 5000, 3);
  ASSERT_EQ(applied.size(), 3u);
  EXPECT_EQ(applied[0].seq, 3);
  EXPECT_EQ(applied[1].seq, 4);
  EXPECT_EQ(applied[2].seq, 5);
  EXPECT_EQ(ingest.ackFrontier(), 5);
}

TEST(EventIngest, SteppingOverADeadSeqStillAppliesWhatWasBufferedInsideIt) {
  // The dead range is "seqs the watch will never send again" — which is not
  // the same as "seqs the boat never received". Seq 3 arrived and is sitting
  // in the reorder buffer; the jump crosses it because the watch's head has
  // moved past, and dropping it here would lose a mode change that was
  // delivered perfectly well, inside the fix for losing mode changes.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_MOTOR, 3000, 2).empty());
  ASSERT_EQ(ingest.ackFrontier(), 1);

  // Seqs 2 and 4 never arrived and the watch has dropped both: its head is 5.
  auto applied = acceptMode(ingest, 5, moana::MODE_ANCHOR, 5000, 5);
  ASSERT_EQ(applied.size(), 2u);
  EXPECT_EQ(applied[0].seq, 3);  // recovered from behind the jump, in order
  EXPECT_EQ(applied[0].mode, moana::MODE_MOTOR);
  EXPECT_EQ(applied[1].seq, 5);
  EXPECT_EQ(ingest.ackFrontier(), 5);
}

TEST(EventIngest, RecoveredEventsComeBackInSeqOrder) {
  // Several buffered inside one dead range, offered to the buffer in an order
  // that is not their seq order: the caller writes these to the sidecar, so
  // the order they come back in is the order the file ends up in.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);
  EXPECT_TRUE(acceptMode(ingest, 5, moana::MODE_ANCHOR, 5000, 2).empty());
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_MOTOR, 3000, 2).empty());
  EXPECT_TRUE(acceptMode(ingest, 4, moana::MODE_ROW, 4000, 2).empty());

  // The watch has dropped everything up to and including 6.
  auto applied = acceptMode(ingest, 7, moana::MODE_OTHER, 7000, 7);
  ASSERT_EQ(applied.size(), 4u);
  EXPECT_EQ(applied[0].seq, 3);
  EXPECT_EQ(applied[1].seq, 4);
  EXPECT_EQ(applied[2].seq, 5);
  EXPECT_EQ(applied[3].seq, 7);
  EXPECT_EQ(ingest.ackFrontier(), 7);
}

TEST(EventIngest, ARecoveredEventIsHandedBackOnlyOnce) {
  // Its slot is freed on the way past, so the next burst — which repeats the
  // same head — has nothing left to recover and the sidecar gets one line.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_MOTOR, 3000, 2).empty());
  ASSERT_EQ(acceptMode(ingest, 5, moana::MODE_ANCHOR, 5000, 5).size(), 2u);

  EXPECT_TRUE(acceptMode(ingest, 5, moana::MODE_ANCHOR, 5000, 5).empty());
  EXPECT_EQ(ingest.ackFrontier(), 5);
}

TEST(EventIngest, APacketLostTwiceRunningIsStillWaitedFor) {
  // ESP-NOW broadcast has no retry, so the same event's packet being lost in
  // two consecutive bursts is ordinary interference. The old resync read a
  // re-offer of a buffered seq as proof nothing earlier existed and skipped
  // seq 30 for good, acking a mode change that was never applied. The head
  // says otherwise: the watch still holds 30, so the boat waits for it.
  EventIngest ingest;  // fresh boat; the watch holds 30..32

  EXPECT_TRUE(acceptMode(ingest, 31, moana::MODE_MOTOR, 31, 30).empty());
  EXPECT_TRUE(acceptMode(ingest, 32, moana::MODE_ANCHOR, 32, 30).empty());
  EXPECT_TRUE(acceptMode(ingest, 31, moana::MODE_MOTOR, 31, 30).empty());
  EXPECT_TRUE(acceptMode(ingest, 32, moana::MODE_ANCHOR, 32, 30).empty());
  EXPECT_EQ(ingest.ackFrontier(), 29);  // below the head, and no further

  auto applied = acceptMode(ingest, 30, moana::MODE_SAIL, 30, 30);
  ASSERT_EQ(applied.size(), 3u);
  EXPECT_EQ(applied[0].mode, moana::MODE_SAIL);
  EXPECT_EQ(applied[1].mode, moana::MODE_MOTOR);
  EXPECT_EQ(applied[2].mode, moana::MODE_ANCHOR);
  EXPECT_EQ(ingest.ackFrontier(), 32);
}

TEST(EventIngest, RepeatOfBufferedSeqNeverSkipsAHoleTheWatchStillHolds) {
  // With an established frontier and a head that is exactly what we are
  // waiting for, repeats must keep waiting however many times they arrive.
  EventIngest ingest;
  acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1);
  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_OTHER, 3000, 2).empty());
  }
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, SteppingOverADeadSeqFollowsTheCircleAcrossTheWrap) {
  // The dead range can straddle the wrap, where the seq before the watch's
  // head is 0xFFFF rather than 0 — moana::prevEventSeq skips the reserved
  // kEventSeqNone exactly as nextEventSeq does, or the frontier would land on
  // "nothing acked yet" and re-offer the whole session.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 0xFFFE, moana::MODE_SAIL, 1, 0xFFFE).size(), 1u);
  ASSERT_EQ(ingest.ackFrontier(), 0xFFFE);

  // The watch wrapped to seq 1 and dropped the unacked 0xFFFF on the way.
  auto applied = acceptMode(ingest, 1, moana::MODE_MOTOR, 2, 1);
  ASSERT_EQ(applied.size(), 1u);
  EXPECT_EQ(applied[0].seq, 1);
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, AHeadAheadOfItsOwnPacketIsNotBelieved) {
  // A watch never sends a head above the seq in the same packet — its queue
  // is in seq order and the head is the first thing sent. A frame that says
  // otherwise is damaged or foreign, and taking its word would ack real
  // events nobody ever delivered.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);

  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_MOTOR, 3000, 500).empty());
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, ASilentHeadLeavesTheContiguousRuleAlone) {
  // kEventSeqNone means "the watch declined to say" — this firmware always
  // says, but the frontier must not move on a value that carries no claim.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 1000, 1).size(), 1u);

  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_MOTOR, 3000,
                         moana::kEventSeqNone)
                  .empty());
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, CarriesAnyModeAndKindThrough) {
  // Ingest is seq bookkeeping only: it must hand back whatever kind/mode the
  // packet carried — including a mode this firmware doesn't know — so the
  // caller can ack a newer watch's events instead of resending forever
  // (moana::sailingModeForEvent decides what is applicable).
  EventIngest ingest;
  const uint8_t kAll[] = {
      moana::MODE_SAIL,      moana::MODE_MOTORSAIL, moana::MODE_MOTOR,
      moana::MODE_ROW,       moana::MODE_UNPOWERED, moana::MODE_ANCHOR,
      moana::MODE_ALONGSIDE, moana::MODE_BEACHED,   moana::MODE_TRAILERED,
      moana::MODE_OTHER,
  };
  for (size_t i = 0; i < sizeof(kAll) / sizeof(kAll[0]); i++) {
    const uint16_t seq = (uint16_t)(i + 1);
    auto applied = acceptMode(ingest, seq, kAll[i], 1000 + i, seq);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied[0].kind, moana::EVENT_MODE_CHANGE);
    EXPECT_EQ(applied[0].mode, kAll[i]);
  }

  auto future = ingest.accept(wirePacket(11, 99, 200, 9999, 11));
  ASSERT_EQ(future.size(), 1u);
  EXPECT_EQ(future[0].kind, 99);
  EXPECT_EQ(future[0].mode, 200);
  EXPECT_EQ(ingest.ackFrontier(), 11);  // acked, so the watch stops resending
}

TEST(EventIngest, PendingBufferOverflowDropsQuietly) {
  // Fill the reorder buffer with a long out-of-order run; extras are
  // dropped (the 1/s resend re-offers them) and nothing crashes or acks.
  EventIngest ingest;
  for (uint16_t s = 2; s < 2 + EventIngest::kMaxPending + 4; s++) {
    EXPECT_TRUE(acceptMode(ingest, s, moana::MODE_SAIL, s, 1).empty());
  }
  EXPECT_EQ(ingest.ackFrontier(), moana::kEventSeqNone);

  // Filling the hole applies seq 1 plus every event that found a slot.
  auto applied = acceptMode(ingest, 1, moana::MODE_MOTOR, 1, 1);
  EXPECT_EQ(applied.size(), 1u + EventIngest::kMaxPending);
  EXPECT_EQ(ingest.ackFrontier(), 1 + EventIngest::kMaxPending);
}

// --- holding the frontier for a caller that couldn't record (issue #180) ---

TEST(EventIngest, AppliedEventsCarryTheSeqTheyAdvancedTo) {
  // The caller needs the seq to know how far it may ack, and a drained batch
  // reports each one separately.
  EventIngest ingest;
  EXPECT_TRUE(acceptMode(ingest, 2, moana::MODE_MOTOR, 200, 1).empty());
  auto applied = acceptMode(ingest, 1, moana::MODE_SAIL, 100, 1);

  ASSERT_EQ(applied.size(), 2u);
  EXPECT_EQ(applied[0].seq, 1);
  EXPECT_EQ(applied[1].seq, 2);
}

TEST(EventIngest, HoldingTheFrontierOffersTheEventAgainRatherThanDedupingIt) {
  // Rewinding has to move the *internal* frontier, not just what the caller
  // publishes: leave it forward and the watch's resend reads as a duplicate,
  // the sidecar is never offered the event a second time, and the retry the
  // whole hold exists for never happens.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 100, 1).size(), 1u);

  ingest.holdFrontierAt(moana::kEventSeqNone);  // the sidecar refused it
  EXPECT_EQ(ingest.ackFrontier(), moana::kEventSeqNone);

  auto retry = acceptMode(ingest, 1, moana::MODE_SAIL, 100, 1);
  ASSERT_EQ(retry.size(), 1u);
  EXPECT_EQ(retry[0].mode, moana::MODE_SAIL);
  EXPECT_EQ(ingest.ackFrontier(), 1);
}

TEST(EventIngest, HoldingPartWayThroughABatchKeepsWhatLanded) {
  EventIngest ingest;
  EXPECT_TRUE(acceptMode(ingest, 3, moana::MODE_ANCHOR, 300, 1).empty());
  EXPECT_TRUE(acceptMode(ingest, 2, moana::MODE_MOTOR, 200, 1).empty());
  auto applied = acceptMode(ingest, 1, moana::MODE_SAIL, 100, 1);
  ASSERT_EQ(applied.size(), 3u);

  ingest.holdFrontierAt(1);  // 1 written, 2 refused, 3 never attempted
  EXPECT_EQ(ingest.ackFrontier(), 1);

  // 1 is a duplicate again; 2 and 3 come back around and re-apply in order.
  EXPECT_TRUE(acceptMode(ingest, 1, moana::MODE_SAIL, 100, 1).empty());
  auto redelivered = acceptMode(ingest, 2, moana::MODE_MOTOR, 200, 2);
  ASSERT_EQ(redelivered.size(), 1u);
  EXPECT_EQ(redelivered[0].seq, 2);
  ASSERT_EQ(acceptMode(ingest, 3, moana::MODE_ANCHOR, 300, 2).size(), 1u);
  EXPECT_EQ(ingest.ackFrontier(), 3);
}

TEST(EventIngest, AHeldFrontierSurvivesTheNextBurstsDeclaredHead) {
  // The two mechanisms meet here (issues #177 and #180). The hold rewinds the
  // frontier under an event the sidecar refused, and the very next packet
  // carries the watch's head — which is that same refused seq, because the
  // watch was never told to trim it. The head must read as "still coming",
  // not as a dead gap to step over: stepping would re-ack the event the hold
  // exists to keep un-acked, and the server would never see it.
  EventIngest ingest;
  ASSERT_EQ(acceptMode(ingest, 1, moana::MODE_SAIL, 100, 1).size(), 1u);
  ASSERT_EQ(acceptMode(ingest, 2, moana::MODE_MOTOR, 200, 1).size(), 1u);

  ingest.holdFrontierAt(1);  // the sidecar refused seq 2

  auto retry = acceptMode(ingest, 2, moana::MODE_MOTOR, 200, 2);
  ASSERT_EQ(retry.size(), 1u);
  EXPECT_EQ(retry[0].seq, 2);
  EXPECT_EQ(ingest.ackFrontier(), 2);
}

}  // namespace
