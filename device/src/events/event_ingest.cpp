#include "events/event_ingest.h"

std::vector<moana::Event> EventIngest::accept(const moana::Event& event) {
  std::vector<moana::Event> applied;

  if (event.seq == moana::kEventSeqNone) {
    return applied;  // reserved seq — never assigned by a well-behaved watch
  }

  // The watch's oldest unacked seq is the head of its queue, so anything
  // below it is gone for good — acked already, or dropped on overflow. If the
  // frontier is waiting on one of those, no resend will ever arrive: step
  // over the whole dead range in one go (issue #177). Only ever forwards; a
  // stale frame carries a stale-but-still-safe value, never a lower frontier.
  if (moana::eventSeqGapIsPermanent(event.oldest_unacked_seq, frontier_,
                                    event.seq)) {
    frontier_ = moana::prevEventSeq(event.oldest_unacked_seq);
    recoverPendingBehindFrontier(&applied);
    drainContiguous(&applied);
  }

  if (moana::eventSeqAcked(event.seq, frontier_)) {
    return applied;  // duplicate: already at or behind the ack frontier
  }

  if (!moana::eventSeqIsNext(event.seq, frontier_)) {
    // Past a hole the watch still expects to fill: buffer it, but the
    // frontier must not advance until the missing event arrives (the watch
    // resends everything unacked 1/s).
    for (moana::Event& slot : pending_) {
      if (slot.seq == event.seq) {
        return applied;  // already buffered
      }
    }
    for (moana::Event& slot : pending_) {
      if (slot.seq == moana::kEventSeqNone) {
        slot = event;
        return applied;
      }
    }
    return applied;  // buffer full — drop; the 1/s resend re-offers it
  }

  // Contiguous: advance the frontier, then drain any buffered successors
  // that are now contiguous too.
  frontier_ = event.seq;
  applied.push_back(event);
  drainContiguous(&applied);
  return applied;
}

void EventIngest::drainContiguous(std::vector<moana::Event>* applied) {
  moana::Event next;
  while (takePending(moana::nextEventSeq(frontier_), &next)) {
    frontier_ = next.seq;
    applied->push_back(next);
  }
}

void EventIngest::recoverPendingBehindFrontier(
    std::vector<moana::Event>* applied) {
  // Selection sort over at most kMaxPending slots: the caller applies these in
  // order (they become sidecar lines), the slots are unordered, and a jump is
  // rare enough that clarity beats an index.
  while (true) {
    moana::Event* lowest = nullptr;
    for (moana::Event& slot : pending_) {
      if (slot.seq == moana::kEventSeqNone ||
          !moana::eventSeqAcked(slot.seq, frontier_)) {
        continue;  // free, or still ahead of the frontier — leave it buffered
      }
      if (lowest == nullptr || moana::eventSeqAcked(slot.seq, lowest->seq)) {
        lowest = &slot;
      }
    }
    if (lowest == nullptr) {
      return;
    }
    applied->push_back(*lowest);
    lowest->seq = moana::kEventSeqNone;
  }
}

bool EventIngest::takePending(uint16_t seq, moana::Event* out) {
  for (moana::Event& slot : pending_) {
    if (slot.seq != moana::kEventSeqNone && slot.seq == seq) {
      *out = slot;
      slot.seq = moana::kEventSeqNone;
      return true;
    }
  }
  return false;
}
