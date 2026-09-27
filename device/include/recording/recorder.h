#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>

#include "hardware/gps.h"
#include "hardware/imu_types.h"
#include "recording/recorded_sample.h"
#include "recording/recording_state_machine.h"
#include "storage/littlefs_filesystem.h"
#include "types.h"

class Recorder {
 public:
  using IsGPSLockedFn = std::function<bool()>;
  using StateChangeFn = std::function<void(RecordingState oldState, RecordingState newState)>;
  // Returns the IMU window mean (or instantaneous fallback) and resets the
  // accumulator. Called once per GPS fix at enqueue time, never at flush time.
  using DrainIMUFn = std::function<IMUSnapshot()>;

  Recorder();

  void init(IsGPSLockedFn isGPSLocked, StateChangeFn onStateChange, DrainIMUFn drainIMU);

  void toggle();
  // Absolute set-on/set-off (remote command path, issue #138). Idempotent:
  // a SET to the current state is a no-op, never an error.
  void setRecording(bool on);
  void feedGPSData(const GPSData& data);

  // Append one watch event to the active track's JSONL sidecar (issue #139):
  // "/gps_X.csv" gets "/gps_X.events.jsonl" beside it, carried to SD by the
  // same sync flow. NO_SESSION when nothing is recording — by design events
  // outside a recording only shape the boat's current_mode echo; with no
  // track there is nothing to attach them to. FAILED when the line was
  // supposed to land and didn't, which the caller must not treat as the same
  // answer (issue #180): acking a FAILED event drops the mode change at both
  // ends. Called from the app-controller task; open-append-close per event,
  // on a different file than the recorder task's CSV (LittleFS access is
  // already cross-task, e.g. Logger).
  EventPersist appendEvent(uint8_t kind, uint8_t mode, uint32_t utcSeconds);

  RecordingState getState() const { return state; }

 private:
  // Thread safety: AppController calls toggle() which sets STARTING/STOPPING,
  // then the Recorder task owns all further transitions until it exits.
  // Atomic ensures cross-task reads (e.g. feedGPSData, getState) see current values.
  std::atomic<RecordingState> state;
  IsGPSLockedFn isGPSLocked;
  StateChangeFn onStateChange;
  DrainIMUFn drainIMU;

  QueueHandle_t queue;
  TaskHandle_t taskHandle;
  // Set by stop() (toggle task), polled by the recorder task — a plain cross-
  // task flag, so std::atomic rather than volatile (R5).
  std::atomic<bool> shouldStop;

  // LittleFS adapter the recorder task writes tracks through (via
  // RecordingSession). Stateless wrapper over the global LittleFS mount.
  LittleFSFileSystem trackFs;

  // Sidecar path for the active session ("" when idle). Written by the
  // recorder task at session start/end, read by appendEvent on the
  // app-controller task — spinlock-guarded, fixed buffer (no heap inside
  // the critical section).
  char sidecarPath_[64] = {0};
  portMUX_TYPE sidecarMux_ = portMUX_INITIALIZER_UNLOCKED;
  void setSidecarPath(const char* path);

  void transitionState(RecordingState newState);
  void applyAction(RecordingStateMachine::ToggleAction action);

  void start();
  void stop();
  void addSampleToQueue(const RecordedSample& sample);
  static void task(void* pvParameters);

  static const size_t QUEUE_SIZE = 120;
  static const size_t MAX_BATCH_SIZE = 30;
  static const size_t MIN_BATCH_SIZE = 10;
  // Time backstop for the count-based flush: flush a partial batch once it has
  // been this long since the last write, so the power-cut loss window is bounded
  // in wall-clock time rather than sample count. Above the ~10 s it takes to
  // reach MIN_BATCH_SIZE at 1 Hz, so it adds no extra writes during normal
  // recording — it only kicks in when GPS drops below 1 Hz or goes LOST (R9).
  static const uint32_t FLUSH_INTERVAL_MS = 15000;
};
