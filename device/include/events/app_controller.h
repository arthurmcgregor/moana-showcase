#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <Arduino.h>

#include "espnow_protocol.h"
#include "events/app_event.h"
#include "events/event_ingest.h"
#include "hardware/gps_types.h"
#include "recording/distance_accumulator.h"
#include "storage/sync_types.h"
#include "types.h"
#include "ui/display_cmd.h"

// ============================================
// Subsystem Callbacks — what AppController
// calls into. In production, App wires these
// to real subsystems. In tests, each field is
// a simple lambda.
// ============================================

struct DisplayCallbacks {
  // Single render-side entry point. Switches on cmd.kind internally.
  std::function<void(const DisplayCmd&)> apply;
  // Read-side getter (kept separate; not a command).
  std::function<ScreenState()> getScreenState;
};

struct RecordingCallbacks {
  std::function<void()> toggle;
  // Absolute set-on/set-off (idempotent) — the ESP-NOW command path.
  std::function<void(bool)> setRecording;
  std::function<void(const GPSData&)> feedGPSData;
  std::function<RecordingState()> getState;
  // Append one watch event to the active track's JSONL sidecar (issue #139).
  // NO_SESSION (not recording) still echoes and acks the event; FAILED holds
  // the ack frontier back so the watch resends (issue #180).
  std::function<EventPersist(uint8_t kind, uint8_t mode, uint32_t utcSeconds)>
      appendEvent;
};

struct ConnectivityCallbacks {
  std::function<void()> toggle;
  std::function<void(const GPSData&)> receiveGPSData;
};

struct StorageCallbacks {
  std::function<std::vector<String>()> localTracks;
  std::function<bool(bool, std::function<void(const SyncResult&)>)> startSync;
  std::function<bool()> formatLocal;
};

struct LoggerCallbacks {
  std::function<bool()> initLogger;
};

struct IMUCallbacks {
  // Returns false if the IMU rejected the request (already calibrating, or
  // not in the expected mode). AppController layers the recording-state
  // gate on top: start is blocked when RecordingState != IDLE.
  std::function<bool()> startCalibration;
  std::function<void()> cancelCalibration;
  std::function<bool()> applyPendingCalibration;
  std::function<void()> discardPendingCalibration;
};

// ============================================
// AppController — pure event dispatch logic,
// no FreeRTOS, no hardware, no singletons.
// ============================================

class AppController {
 public:
  struct Callbacks {
    DisplayCallbacks display;
    RecordingCallbacks recording;
    ConnectivityCallbacks connectivity;
    StorageCallbacks storage;
    LoggerCallbacks logger;
    IMUCallbacks imu;
    // Great-circle metres between two (lat, lng) points — production wires
    // TinyGPSPlus::distanceBetween (issue #142). Injected so AppController
    // and its DistanceAccumulator stay natively testable.
    std::function<double(double, double, double, double)> distanceBetween;
    std::function<void(const AppEvent&)> postEvent;
    // Like postEvent but guarantees delivery (blocks rather than dropping) —
    // used for SYNC_COMPLETE, whose loss strands the sync state machine (R3).
    std::function<void(const AppEvent&)> postEventCritical;
    std::function<void()> markBootClean;
  };

  AppController() = default;
  void init(Callbacks callbacks);

  // Single entry point — replaces App::processEvent
  void process(const AppEvent& event);

  // State accessors (for cross-task reads via App)
  GPSState getGPSState() const { return gpsState.load(); }
  SyncState getSyncState() const { return syncState.load(); }
  RecoveryState getRecoveryState() const { return recoveryState; }
  // Watch-event echo state (issue #139), read by composeTelemetry on the
  // ESP-NOW send task — hence atomic.
  uint8_t getCurrentMode() const { return currentMode.load(); }
  uint16_t getEventAckFrontier() const { return eventAckFrontier.load(); }
  // Session distance echo (issue #142): metres integrated between valid
  // fixes while recording is ACTIVE, 0 at start, held after stop. Read by
  // composeTelemetry on the ESP-NOW send task (atomic inside). Also the
  // value the device display will surface for issue #65.
  uint32_t getCumulativeDistanceM() const { return distance.totalMeters(); }

  // For boot recovery (called from App::init before event loop starts)
  void setRecoveryState(RecoveryState state) { recoveryState = state; }
  void startSync();

 private:
  void transitionGPS(GPSState newState);
  void handleButtonPress(ButtonId buttonId);
  void handleModeEvent(const moana::Event& event);
  void onSyncComplete(const SyncResult& result);

  // State. process() only runs on the app-controller task, but these are read
  // from other tasks (getGPSState from GPS-health/touch/recorder, getSyncState
  // from web) and recoveryState is set from loopTask during boot — so they are
  // atomic rather than plain fields (R5).
  std::atomic<GPSState> gpsState{GPSState::NOT_STARTED};
  std::atomic<SyncState> syncState{SyncState::IDLE};
  std::atomic<RecoveryState> recoveryState{RecoveryState::IDLE};

  // Watch-event state (issue #139). process() only mutates these on the
  // app-controller task; composeTelemetry reads them from the ESP-NOW send
  // task. Deliberately RAM only — UNKNOWN / no-events-acked on every boot,
  // matching the watch queue's fresh delivery after a boat restart.
  std::atomic<uint8_t> currentMode{moana::MODE_UNKNOWN};
  std::atomic<uint16_t> eventAckFrontier{moana::kEventSeqNone};
  EventIngest eventIngest;

  // Cumulative session distance (issue #142). Fed on the app-controller task
  // (GPS_DATA fan-out + GPS_STATE transitions); telemetry reads its atomic.
  DistanceAccumulator distance;

  Callbacks cb;
};
