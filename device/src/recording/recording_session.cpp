#include "recording/recording_session.h"

#include <cstring>

#include "recording/gps_csv.h"

RecordingSession::RecordingSession(TrackFileSystem& fs) : fs_(fs) {}

bool RecordingSession::begin(const String& path) {
  file_ = fs_.openWrite(path);
  if (!file_) {
    return false;
  }

  const char* header = GPSCSV::header();
  if (!file_->write(reinterpret_cast<const uint8_t*>(header), std::strlen(header)) ||
      !file_->write(reinterpret_cast<const uint8_t*>("\n"), 1) || !file_->flush()) {
    end();
    return false;
  }
  return true;
}

bool RecordingSession::writeBatch(const RecordedSample* samples, size_t count) {
  if (!file_) {
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    size_t len = GPSCSV::formatRow(rowBuf_, sizeof(rowBuf_), samples[i].capturedAtMs,
                                   samples[i].gps, samples[i].imu);
    // formatRow returns 0 only if the row overflowed the buffer; skip it
    // rather than writing a truncated line, but keep the session alive.
    if (len == 0) {
      continue;
    }
    if (!file_->write(reinterpret_cast<const uint8_t*>(rowBuf_), len) ||
        !file_->write(reinterpret_cast<const uint8_t*>("\n"), 1)) {
      return false;
    }
  }
  return file_->flush();
}

void RecordingSession::end() {
  if (file_) {
    file_->close();
    file_.reset();
  }
}
