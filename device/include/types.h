#pragma once

// Shared enum types used across subsystems.
// This header has minimal dependencies to avoid circular includes.

#include <functional>

enum class ButtonId {
  START_SYNC,
  TOGGLE_WIFI,
  TOGGLE_RECORDING,
  FORMAT,
  START_IMU_CALIBRATION,
  CANCEL_IMU_CALIBRATION,
  APPLY_IMU_CALIBRATION,
  DISCARD_IMU_CALIBRATION,
};

enum class ScreenState {
  MAIN_MENU,           // Main menu with buttons
  SPEED_SCREEN,        // Showing current speed
  DETAIL_SCREEN,       // Detailed GPS info screen
  WIFI_SCREEN,         // Wifi control screen
  SYNC_SCREEN,         // Sync operation screen
  CALIBRATION_SCREEN,  // IMU mag calibration flow
};

enum class DisplaySleepState {
  AWAKE,    // Display is on and active
  SLEEPING  // Display is off to save power
};

enum class GPSState {
  NOT_STARTED,  // Before GPS init
  ACQUIRING,    // Searching for fix (no valid data yet)
  LOCKED,       // Has valid fix, receiving data
  LOST,         // Previously had fix, now lost (UI indication only)
};

enum class RecordingState {
  IDLE,      // Not recording
  STARTING,  // Task starting, not ready for data yet
  ACTIVE,    // Recording GPS fixes to LittleFS
  STOPPING,  // Stop requested, task cleaning up
  FAILED,    // Failed to start recording
};

// What became of one watch event handed to the events sidecar (issue #180).
// The two failures are not the same failure: NO_SESSION is the expected,
// deliberate case (no recording, so nothing to attach the event to) and the
// event is acked so the watch stops resending; FAILED means the sidecar was
// there and would not take the line, and acking that loses the mode change at
// both ends.
enum class EventPersist {
  WRITTEN,     // on disk in the active session's sidecar
  NO_SESSION,  // nothing recording — echoed and acked, deliberately not kept
  FAILED,      // the write was attempted and did not land
};

enum class WifiState {
  DISCONNECTED,  // Wifi off
  CONNECTING,    // Attempting to connect to AP
  CONNECTED,     // Connected to AP, IP assigned
  ERROR,         // Connection failed
};

enum class SyncState {
  IDLE,     // Not syncing
  SYNCING,  // Copying data from LittleFS to SD card
};

enum class RecoveryState {
  IDLE,    // Normal operation
  NEEDED,  // Filesystem corrupted — sync then format required
};

// Callback type for screens to post button press events
using PostEventFn = std::function<void(ButtonId)>;
