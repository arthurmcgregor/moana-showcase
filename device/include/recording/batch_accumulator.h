#pragma once

#include <cstddef>

// Accumulates samples and decides when to flush a batch.
// Pure logic — no FreeRTOS queues, no file I/O. Templated so the same
// machinery serves both pure GPS rows (in unit tests) and the production
// RecordedSample (GPS + IMU) bundle.
template <typename T>
class BatchAccumulator {
 public:
  BatchAccumulator(T* buffer, size_t capacity, size_t minBatch)
      : buffer_(buffer), capacity_(capacity), minBatch_(minBatch) {}

  // Add an item to the batch. Returns true if ready to flush.
  bool add(const T& item) {
    if (count_ < capacity_) {
      buffer_[count_++] = item;
    }
    return count_ >= minBatch_;
  }

  // Force-add for draining remaining items (no threshold check).
  void addNoThreshold(const T& item) {
    if (count_ < capacity_) {
      buffer_[count_++] = item;
    }
  }

  const T* data() const { return buffer_; }
  size_t count() const { return count_; }
  bool readyToFlush() const { return count_ >= minBatch_; }
  bool hasData() const { return count_ > 0; }

  void clear() { count_ = 0; }

 private:
  T* buffer_;
  size_t capacity_;
  size_t minBatch_;
  size_t count_ = 0;
};
