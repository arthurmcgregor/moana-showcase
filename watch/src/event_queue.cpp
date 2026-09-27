#include "event_queue.h"

#include <string.h>

namespace event_queue {

// Little-endian field-by-field serialisation: portable between the ESP32
// target and the x86 native test build, no struct-padding surprises.
static void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static uint16_t get16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

Queue::Queue(Store& store) : store_(store) {
  restore();
}

void Queue::restore() {
  uint8_t buf[kPersistMaxBytes];
  size_t n = store_.load(buf, sizeof(buf));
  if (n < kPersistHeaderBytes || get32(buf) != kPersistMagic) {
    return;  // nothing stored (or unrecognised) — start empty at seq 1
  }
  uint16_t nextSeq = get16(buf + 4);
  uint16_t count = get16(buf + 6);
  if (nextSeq == moana::kEventSeqNone || count > kMaxEvents ||
      n < kPersistHeaderBytes + (size_t)count * kPersistRecordBytes) {
    return;  // corrupt — safer to start empty than replay garbage
  }
  nextSeq_ = nextSeq;
  count_ = count;
  for (size_t i = 0; i < count_; i++) {
    const uint8_t* rec = buf + kPersistHeaderBytes + i * kPersistRecordBytes;
    events_[i].seq = get16(rec);
    events_[i].kind = rec[2];
    events_[i].mode = rec[3];
    events_[i].utc_seconds = get32(rec + 4);
  }
  // Anything restored was queued before the reboot and is by definition
  // unacked — deliver it as soon as the loop starts ticking.
  sendArmed_ = count_ > 0;
}

void Queue::persist() {
  uint8_t buf[kPersistMaxBytes];
  put32(buf, kPersistMagic);
  put16(buf + 4, nextSeq_);
  put16(buf + 6, (uint16_t)count_);
  for (size_t i = 0; i < count_; i++) {
    uint8_t* rec = buf + kPersistHeaderBytes + i * kPersistRecordBytes;
    put16(rec, events_[i].seq);
    rec[2] = events_[i].kind;
    rec[3] = events_[i].mode;
    put32(rec + 4, events_[i].utc_seconds);
  }
  store_.save(buf, kPersistHeaderBytes + count_ * kPersistRecordBytes);
}

uint16_t Queue::push(uint8_t kind, uint8_t mode, uint32_t utcSeconds,
                     uint32_t nowMs) {
  (void)nowMs;
  if (count_ == kMaxEvents) {
    // Full: drop the oldest. The user's newest press must not vanish, and
    // 64 unacked mode changes already means the earliest ones are stale
    // segment boundaries at best. Survivable since issue #177: the next send
    // carries the new head as oldest_unacked_seq, so the boat steps its
    // frontier over the hole instead of freezing on it for the session.
    memmove(&events_[0], &events_[1], (kMaxEvents - 1) * sizeof(events_[0]));
    count_--;
  }
  uint16_t seq = nextSeq_;
  nextSeq_ = moana::nextEventSeq(nextSeq_);
  events_[count_].seq = seq;
  events_[count_].kind = kind;
  events_[count_].mode = mode;
  events_[count_].utc_seconds = utcSeconds;
  count_++;
  persist();
  sendArmed_ = true;
  return seq;
}

bool Queue::tick(uint32_t nowMs, uint16_t ackFrontier, bool ackValid) {
  if (ackValid) {
    // Cumulative ack: everything at or behind the frontier is delivered.
    // Events are stored in seq order, so acked ones form a prefix.
    size_t acked = 0;
    while (acked < count_ &&
           moana::eventSeqAcked(events_[acked].seq, ackFrontier)) {
      acked++;
    }
    if (acked > 0) {
      memmove(&events_[0], &events_[acked],
              (count_ - acked) * sizeof(events_[0]));
      count_ -= acked;
      persist();
    }
  }

  if (count_ == 0) {
    return false;
  }
  if (sendArmed_ || nowMs - lastSendMs_ >= kResendIntervalMs) {
    sendArmed_ = false;
    lastSendMs_ = nowMs;
    return true;
  }
  return false;
}

}  // namespace event_queue
