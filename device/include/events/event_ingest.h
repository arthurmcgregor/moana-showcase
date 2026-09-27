#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "espnow_protocol.h"

// Receive-side half of the watch's store-and-forward event pipeline (issue
// #139): dedupe by seq and contiguous-ack bookkeeping. Pure logic — no
// Arduino, no FreeRTOS — owned by AppController and unit-tested natively.
//
// Rules (TCP-style cumulative ack over uint16 seqs, arithmetic shared via
// the moana::eventSeq* helpers in espnow_protocol.h):
// - An event at or behind the frontier is a duplicate: ignored.
// - The event exactly after the frontier advances it and is applied; any
//   buffered successors that become contiguous are applied in seq order.
// - An event past a hole is buffered (bounded) but never advances the
//   frontier — the watch keeps resending the missing one until acked.
// - Unless the watch says the hole is dead. Every Event carries
//   oldest_unacked_seq, the head of the watch's queue; anything below it will
//   never be offered again, so a frontier waiting on one of those seqs is
//   waiting forever and steps over the whole dead range at once (issue #177).
//   Anything already buffered inside that range is applied on the way past —
//   "the watch will not send this again" is not "the boat never received it".
//
// That last rule is what a cumulative frontier cannot express on its own —
// "I have everything up to S" says nothing about *which* seq is missing — and
// it replaces the inference this class used to make. Two cases it settles:
//
// - Reboot resync: the frontier is RAM-only, but the watch's seq counter
//   persists in NVS, so after a boat reboot the watch keeps sending seqs the
//   fresh frontier (kEventSeqNone, expecting seq 1) reads as one giant hole.
//   The first packet of the first burst now carries the answer, so the
//   backlog applies immediately rather than a resend cycle later.
// - Queue overflow: at kMaxEvents the watch drops its oldest unacked event
//   (watch/src/event_queue.cpp). The hole that leaves can never be filled,
//   and before this field the frontier froze there for the rest of the
//   session — recoverable only by rebooting the boat.
//
// The mirror case — erasing the watch's NVS so its seqs restart below a live
// boat frontier — still needs a boat reboot to resync; that's a bench-only
// operation, and taking the watch's word for a *backwards* jump would let one
// stale frame un-ack real events.
class EventIngest {
 public:
  // Out-of-order arrivals buffered while waiting for a hole to fill. The
  // watch resends all unacked events every second, so anything dropped for
  // want of a slot is re-offered almost immediately.
  static constexpr size_t kMaxPending = 16;

  // Feed one received packet — the wire struct whole, because that is what it
  // is, and because a field added to it should not widen this signature the
  // way oldest_unacked_seq did. Returns the events that became applicable, in
  // seq order (empty for duplicates and out-of-order arrivals). The mode is
  // carried through untouched — validating it is the caller's job
  // (moana::sailingModeForEvent), so a value from a newer watch firmware is
  // still acked rather than resent forever.
  //
  // The packet's oldest_unacked_seq — the lowest seq the sending watch still
  // holds — can make this call apply buffered events the incoming one has
  // nothing to do with, when it proves the gap in front of the frontier is
  // dead. So the return value is everything that became applicable, not
  // everything downstream of `event.seq`, and it can include seqs *behind* the
  // frontier this call jumped to (see recoverPendingBehindFrontier).
  std::vector<moana::Event> accept(const moana::Event& event);

  // Highest contiguous seq received — echoed in every telemetry packet as
  // last_event_seq_acked. moana::kEventSeqNone until the first event.
  uint16_t ackFrontier() const { return frontier_; }

  // Put the frontier back to `seq`, un-acking everything after it (issue
  // #180). The caller applies each event to something with a will of its own —
  // the events sidecar — and an event the sidecar refused must not be acked:
  // the watch would trim it from NVS and the server would never see it.
  //
  // Nothing needs re-buffering. The watch resends every unacked event once a
  // second, so whatever this rewinds over is re-offered almost immediately,
  // and it is re-offered because it was never acked in the first place. Only
  // ever called with a seq at or behind the current frontier.
  void holdFrontierAt(uint16_t seq) { frontier_ = seq; }

 private:
  // A slot holds a whole packet; seq == kEventSeqNone means the slot is free,
  // which the wire guarantees to be unambiguous (the watch never assigns it).
  bool takePending(uint16_t seq, moana::Event* out);
  // Apply every buffered event contiguous with the frontier, in seq order.
  void drainContiguous(std::vector<moana::Event>* applied);
  // Hand back every buffered event the frontier has since jumped over, in seq
  // order, and free its slot. A jump only ever crosses seqs the watch will
  // never send again — but a seq the boat already *holds* is not one of those,
  // and dropping it here would lose a real mode change that arrived safely,
  // inside the fix for losing mode changes (issue #177). They are applied,
  // just no longer able to move a frontier that is already past them.
  void recoverPendingBehindFrontier(std::vector<moana::Event>* applied);

  moana::Event pending_[kMaxPending] = {};
  uint16_t frontier_ = moana::kEventSeqNone;
};
