#include "recording/recorder.h"

#include <cstring>

#include "recording/batch_accumulator.h"
#include "recording/event_jsonl.h"
#include "recording/gps_csv.h"
#include "recording/recording_session.h"
#include "recording/recording_state_machine.h"
#include "storage/track_filename.h"
#include "utils/logger.h"
#include "utils/state_strings.h"

static const char* LOG_TAG = "Recording";

Recorder::Recorder()
    : state(RecordingState::IDLE), queue(nullptr), taskHandle(nullptr), shouldStop(false) {
  queue = xQueueCreate(QUEUE_SIZE, sizeof(RecordedSample));
}

void Recorder::init(IsGPSLockedFn isGPSLocked, StateChangeFn onStateChange, DrainIMUFn drainIMU) {
  this->isGPSLocked = isGPSLocked;
  this->onStateChange = onStateChange;
  this->drainIMU = drainIMU;
}

// ============================================
// State Management
// ============================================

void Recorder::transitionState(RecordingState newState) {
  if (state == newState) {
    return;
  }

  // GPS lock check for IDLE → STARTING
  if (state == RecordingState::IDLE && newState == RecordingState::STARTING && !isGPSLocked()) {
    LOG_WARN(LOG_TAG, "Cannot start recording - GPS not locked");
    return;
  }

  if (!RecordingStateMachine::isValidTransition(state, newState)) {
    LOG_WARN(LOG_TAG, "Invalid recording transition: %s -> %s", recordingStateToString(state),
             recordingStateToString(newState));
    return;
  }

  LOG_INFO(LOG_TAG, "Recording transition: %s -> %s", recordingStateToString(state),
           recordingStateToString(newState));

  RecordingState oldState = state;
  state = newState;

  if (onStateChange) {
    onStateChange(oldState, newState);
  }
}

void Recorder::toggle() {
  applyAction(RecordingStateMachine::toggleAction(state));
}

void Recorder::setRecording(bool on) {
  applyAction(on ? RecordingStateMachine::setOnAction(state)
                 : RecordingStateMachine::setOffAction(state));
}

void Recorder::applyAction(RecordingStateMachine::ToggleAction action) {
  using Action = RecordingStateMachine::ToggleAction;

  switch (action) {
    case Action::START:
      transitionState(RecordingState::STARTING);
      if (state == RecordingState::STARTING) {
        start();
      }
      break;
    case Action::STOP:
      transitionState(RecordingState::STOPPING);
      if (state == RecordingState::STOPPING) {
        stop();
      }
      break;
    case Action::RESET:
      transitionState(RecordingState::IDLE);
      break;
    case Action::NONE:
      LOG_DEBUG(LOG_TAG, "Recording state unchanged, ignoring request");
      break;
  }
}

void Recorder::setSidecarPath(const char* path) {
  portENTER_CRITICAL(&sidecarMux_);
  strncpy(sidecarPath_, path, sizeof(sidecarPath_) - 1);
  sidecarPath_[sizeof(sidecarPath_) - 1] = '\0';
  portEXIT_CRITICAL(&sidecarMux_);
}

EventPersist Recorder::appendEvent(uint8_t kind, uint8_t mode,
                                   uint32_t utcSeconds) {
  // Snapshot the path under the lock; the actual I/O happens outside it.
  char path[sizeof(sidecarPath_)];
  portENTER_CRITICAL(&sidecarMux_);
  memcpy(path, sidecarPath_, sizeof(path));
  portEXIT_CRITICAL(&sidecarMux_);

  if (path[0] == '\0') {
    return EventPersist::NO_SESSION;  // echoed + acked, deliberately not kept
  }

  char line[64];
  size_t len =
      EventJsonl::formatLine(line, sizeof(line), kind, mode, utcSeconds);
  if (len == 0) {
    LOG_ERROR(LOG_TAG, "Event line formatting failed (kind %u mode %u)", kind,
              mode);
    return EventPersist::FAILED;
  }

  // Open-append-close per event: events are rare (a handful per sail), and
  // holding no handle means no cross-task file-state sharing with the
  // recorder task's CSV writes.
  auto file = trackFs.openAppend(String(path));
  if (!file) {
    LOG_ERROR(LOG_TAG, "Failed to open events sidecar: %s", path);
    return EventPersist::FAILED;
  }
  bool ok = file->write(reinterpret_cast<const uint8_t*>(line), len) &&
            file->flush();
  file->close();
  if (!ok) {
    LOG_ERROR(LOG_TAG, "Failed to append event to sidecar: %s", path);
    return EventPersist::FAILED;
  }
  LOG_INFO(LOG_TAG, "Event %s (utc %lu) -> %s",
           EventJsonl::kindName(kind, mode), (unsigned long)utcSeconds, path);
  return EventPersist::WRITTEN;
}

void Recorder::feedGPSData(const GPSData& data) {
  if (state == RecordingState::ACTIVE) {
    // Snapshot IMU now, not at flush time. Each row in the batch reflects
    // the IMU window between this fix and the previous fix; flush is
    // batched (~10 fixes) so a flush-time drain would smear the same
    // value across all rows in the batch.
    RecordedSample sample;
    sample.gps = data;
    sample.imu = drainIMU ? drainIMU() : IMUSnapshot{0, 0, 0, false};
    // Stamp capture time here, at fix arrival — not at flush time. The row may
    // sit in the batch queue for ~10-30 s before it is written; using millis()
    // in writeBatch would give every row in a batch one late, identical value.
    sample.capturedAtMs = millis();
    addSampleToQueue(sample);
  }
}

// ============================================
// Recording Operations
// ============================================

void Recorder::start() {
  LOG_INFO(LOG_TAG, "Starting recording session...");
  shouldStop = false;
  // Drop any samples left over from a previous session (e.g. one that aborted
  // to FAILED and skipped its final drain). The queue outlives individual
  // sessions, so without this the new task's first write would prepend the old
  // track's leftover samples — with their stale capturedAtMs — to this track.
  if (queue != nullptr) {
    xQueueReset(queue);
  }
  xTaskCreate(Recorder::task, "Recorder", 8192, this, 2, &taskHandle);
}

void Recorder::stop() {
  LOG_INFO(LOG_TAG, "Signaling recorder task to stop...");
  // The task blocks on the queue with a short timeout (see task()), so it
  // observes shouldStop within one poll interval. No dummy item is enqueued:
  // the old wake-with-dummy trick could either discard the real samples drained
  // alongside it, or write the all-zeros dummy to the CSV as a 0,0 row.
  shouldStop = true;
  taskHandle = nullptr;
}

void Recorder::addSampleToQueue(const RecordedSample& sample) {
  if (queue == nullptr) {
    return;
  }

  UBaseType_t queueWaiting = uxQueueMessagesWaiting(queue);
  if (queueWaiting > 50) {
    LOG_WARN(LOG_TAG, "Recorder queue filling up: %d/%d items", queueWaiting, QUEUE_SIZE);
  }

  if (xQueueSend(queue, &sample, 0) != pdTRUE) {
    LOG_ERROR(LOG_TAG, "Recorder queue full, GPS data dropped");
  }
}

void Recorder::task(void* pvParameters) {
  Recorder* recording = static_cast<Recorder*>(pvParameters);
  LOG_INFO(LOG_TAG, "Recorder task started");

  String filename = GPSCSV::generateFilename();
  LOG_INFO(LOG_TAG, "Recording to: %s", filename.c_str());

  RecordingSession session(recording->trackFs);
  if (!session.begin(filename)) {
    LOG_ERROR(LOG_TAG, "Failed to open file: %s", filename.c_str());
    recording->transitionState(RecordingState::FAILED);
    vTaskDelete(nullptr);
    return;
  }

  // Publish the events-sidecar path before going ACTIVE so a mode event
  // that races the transition never sees ACTIVE with an empty path. The
  // sidecar file itself is created lazily on the first event — a track
  // with no mode changes gets no (empty) sidecar.
  recording->setSidecarPath(
      TrackFilename::eventSidecarFor(filename).c_str());

  recording->transitionState(RecordingState::ACTIVE);
  LOG_INFO(LOG_TAG, "Recorder ready to receive data");

  RecordedSample batchBuffer[MAX_BATCH_SIZE];
  BatchAccumulator<RecordedSample> batch(batchBuffer, MAX_BATCH_SIZE, MIN_BATCH_SIZE);

  bool writeFailed = false;
  uint32_t lastFlushMs = millis();

  // Write the current batch to the file and reset the flush clock. Returns
  // false if the write failed (disk full → abort). No-op on an empty batch.
  auto flush = [&]() -> bool {
    if (!batch.hasData()) {
      return true;
    }
    if (!session.writeBatch(batch.data(), batch.count())) {
      return false;
    }
    LOG_DEBUG(LOG_TAG, "Wrote %d GPS points to file", batch.count());
    batch.clear();
    lastFlushMs = millis();
    return true;
  };

  // Feed one drained sample into the accumulator; flush when it reaches
  // MIN_BATCH_SIZE.
  auto absorb = [&](const RecordedSample& s) -> bool {
    if (batch.add(s)) {
      return flush();
    }
    return true;
  };

  // Poll the queue with a short timeout rather than blocking forever, so a
  // stop() (which only sets shouldStop) is observed promptly without needing a
  // sentinel item in the data queue. Samples are received one at a time into a
  // single stack slot: xQueueReceive returns immediately while items are
  // queued (the timeout only applies when empty), and bulk drain buffers here
  // (2 × MAX_BATCH_SIZE × ~88 B) overflowed the 8 KB task stack alongside
  // batchBuffer.
  RecordedSample sample;
  while (!recording->shouldStop && !writeFailed) {
    if (xQueueReceive(recording->queue, &sample, pdMS_TO_TICKS(250)) == pdTRUE) {
      if (!absorb(sample)) {
        writeFailed = true;
      }
    } else if (batch.hasData() && millis() - lastFlushMs >= FLUSH_INTERVAL_MS) {
      // Queue went quiet (GPS below 1 Hz or LOST) with a partial batch held in
      // RAM — flush it so the time at risk stays bounded (R9). The receive
      // timeout guarantees we re-check a few times a second while idle.
      if (!flush()) {
        writeFailed = true;
      }
    }
  }

  // Drain whatever real samples are still queued (and the partial batch) so the
  // end of the track is not lost — unless a write already failed, in which case
  // further writes would fail too.
  if (!writeFailed) {
    LOG_INFO(LOG_TAG, "Recorder task received stop signal");
    size_t totalFlushed = 0;
    while (!writeFailed && xQueueReceive(recording->queue, &sample, 0) == pdTRUE) {
      if (!absorb(sample)) {
        writeFailed = true;
        break;
      }
      totalFlushed++;
    }
    if (!writeFailed && batch.hasData()) {
      size_t partial = batch.count();
      if (session.writeBatch(batch.data(), partial)) {
        totalFlushed += partial;
        batch.clear();
      } else {
        writeFailed = true;
      }
    }
    if (totalFlushed > 0) {
      LOG_INFO(LOG_TAG, "Flushed %d GPS points from queue before exit", totalFlushed);
    }
  }

  session.end();
  recording->setSidecarPath("");  // session over — stop persisting events
  LOG_INFO(LOG_TAG, "File closed: %s", filename.c_str());

  if (writeFailed) {
    LOG_ERROR(LOG_TAG, "Write failed (filesystem full?) — recording aborted");
    recording->transitionState(RecordingState::FAILED);
  } else {
    recording->transitionState(RecordingState::IDLE);
  }

  LOG_INFO(LOG_TAG, "Recorder task exiting");
  vTaskDelete(nullptr);
}
