// Flash-backed store-and-forward queue for watch -> boat Event packets
// (issue #139).
//
// Pure decision logic, same pattern as command_channel: no Arduino, no LVGL,
// no ESP-NOW includes — push() assigns seqs, tick(now_ms, ack_frontier)
// decides when to resend, and the caller (ui.cpp) does the radio I/O via
// espnow_link. Unit-tested in test/test_native/.
//
// Semantics (store-and-forward with cumulative ack):
// - push() assigns the next seq (never reused, survives reboot), appends,
//   and persists the whole queue through the Store port.
// - tick() trims every event covered by the boat's echoed contiguous-ack
//   frontier (moana::eventSeqAcked), then asks for a resend-all-unacked
//   burst once per second while anything is pending — a lost packet
//   self-heals within ~1 s, and the boat dedupes by seq.
// - Persistence goes through the Store port: production adapter on NVS
//   (event_store_nvs), in-memory fake in tests. Mirrors the device's
//   TrackFileSystem port pattern.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "espnow_protocol.h"

namespace event_queue {

// More than a full day of mode fiddling while out of range; ~450 B persisted.
constexpr size_t kMaxEvents = 64;
constexpr uint32_t kResendIntervalMs = 1000;

// Serialised snapshot: header + count records (see persist()/restore()).
// The magic doubles as the record-layout version: bumped to "MEV2" when the
// mode byte joined each record (issue #146), so a pre-#146 blob is simply not
// recognised and the queue starts empty at seq 1. That loses the persistent
// seq counter across the upgrade, which is safe *because* the upgrade is
// lockstep: the boat is reflashed too, and its ack frontier is RAM-only, so
// both ends restart their numbering together.
constexpr uint32_t kPersistMagic = 0x4D455632;  // "MEV2"
constexpr size_t kPersistHeaderBytes = 4 + 2 + 2;
constexpr size_t kPersistRecordBytes = 2 + 1 + 1 + 4;
constexpr size_t kPersistMaxBytes =
    kPersistHeaderBytes + kMaxEvents * kPersistRecordBytes;

struct StoredEvent {
  uint16_t seq;
  uint8_t kind;          // moana::EventKind
  uint8_t mode;          // moana::SailingMode (EVENT_MODE_CHANGE only)
  uint32_t utc_seconds;  // 0 = time unknown
};

// The wire packet for one queued event. `version`/`type` are left to the
// transport (espnow_link::sendEvent), which is the only thing that should know
// them; everything else on the wire comes from here.
//
// A packet is one bundle and travels as one — the alternative is a parameter
// list that grows by one every time the protocol does, at every hop it passes
// through. `oldestUnackedSeq` is the head of the queue at send time (issue
// #177), the same value for every packet in a resend burst: it is a statement
// about the queue, not about the event beside it.
inline moana::Event wireEvent(const StoredEvent& e, uint16_t oldestUnackedSeq) {
  moana::Event ev = {};
  ev.seq = e.seq;
  ev.kind = e.kind;
  ev.mode = e.mode;
  ev.utc_seconds = e.utc_seconds;
  ev.oldest_unacked_seq = oldestUnackedSeq;
  return ev;
}

// Persistence port. load() fills buf and returns the byte count (0 = nothing
// stored); save() persists an opaque blob. Adapters: NVS in production, an
// in-memory struct in tests.
class Store {
 public:
  virtual ~Store() = default;
  virtual size_t load(uint8_t* buf, size_t cap) = 0;
  virtual bool save(const uint8_t* data, size_t len) = 0;
};

class Queue {
 public:
  // Restores the queue (including the next seq to assign) from the store.
  // A missing or corrupt blob starts empty at seq 1.
  explicit Queue(Store& store);

  // Queue a new event: assigns the next seq (never reusing one, even across
  // reboots), persists, and arms an immediate send on the next tick.
  // When full the oldest unacked event is dropped to make room — the newest
  // press is the one the user just made and must not be lost. That event is
  // gone for good, but delivery of everything after it is not: the hole it
  // leaves is declared on the wire (see oldestUnackedSeq).
  // `mode` is the moana::SailingMode for EVENT_MODE_CHANGE, MODE_UNKNOWN for
  // other kinds. Returns the assigned seq.
  uint16_t push(uint8_t kind, uint8_t mode, uint32_t utcSeconds,
                uint32_t nowMs);

  // Call once per UI tick with the boat's echoed ack frontier (ackValid =
  // false when no telemetry has ever been received). Trims acked events,
  // then returns true when the caller should transmit ALL pending events
  // now (immediately after a push, then once per second while any remain).
  bool tick(uint32_t nowMs, uint16_t ackFrontier, bool ackValid);

  size_t pendingCount() const { return count_; }
  const StoredEvent& pendingAt(size_t i) const { return events_[i]; }

  // The lowest seq still queued — stamped into every Event packet as
  // oldest_unacked_seq (issue #177). It is the boat's only way to tell a gap
  // that will fill from one that never can: seqs below this were either acked
  // or dropped on overflow, and either way no resend will ever offer them
  // again, so a boat waiting on one is waiting for the rest of the session.
  // Events are stored in seq order, so this is simply the head.
  // moana::kEventSeqNone when nothing is queued — in which case there is no
  // packet to stamp it into either.
  uint16_t oldestUnackedSeq() const {
    return count_ > 0 ? events_[0].seq : moana::kEventSeqNone;
  }

 private:
  void persist();
  void restore();

  Store& store_;
  StoredEvent events_[kMaxEvents];
  size_t count_ = 0;
  uint16_t nextSeq_ = 1;  // first seq ever assigned; kEventSeqNone is skipped
  uint32_t lastSendMs_ = 0;
  bool sendArmed_ = false;  // forces a send on the next tick (fresh push/boot)
};

}  // namespace event_queue
