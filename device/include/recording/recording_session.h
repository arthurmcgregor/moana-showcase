#pragma once

#include <cstddef>
#include <memory>

#include "recording/recorded_sample.h"
#include "storage/track_filesystem.h"

// Owns one open track file for the lifetime of a recording session.
//
// Extracted from Recorder so the data-loss-critical file paths (open failure,
// header write, per-batch flush cadence, write-failure detection) are testable
// against a FakeFileSystem instead of only on hardware. All FreeRTOS/queue
// concerns stay in Recorder; this class is pure, synchronous file I/O over the
// TrackFileSystem seam.
class RecordingSession {
 public:
  explicit RecordingSession(TrackFileSystem& fs);

  // Open the file and write the CSV header. Returns false if the file could
  // not be opened or the header could not be written (session left closed).
  bool begin(const String& path);

  // Format each sample as a CSV row (timestamp = sample.capturedAtMs) and
  // write it, flushing once at the end. Returns false on the first write or
  // flush failure — the recorder treats this as fatal (disk full) and aborts.
  // Rows already written before the failure remain in the file.
  bool writeBatch(const RecordedSample* samples, size_t count);

  // Close the file. Safe to call when already closed.
  void end();

  bool isOpen() const { return file_ != nullptr; }

 private:
  TrackFileSystem& fs_;
  std::unique_ptr<TrackFileSystem::WriteFile> file_;
  // Sized to comfortably hold one formatted CSV row (13 columns of numbers).
  char rowBuf_[200];
};
