#include "utils/state_strings.h"

const char* screenStateToString(ScreenState state) {
  switch (state) {
    case ScreenState::MAIN_MENU:
      return "MAIN_MENU";
    case ScreenState::SPEED_SCREEN:
      return "SPEED_SCREEN";
    case ScreenState::DETAIL_SCREEN:
      return "DETAIL_SCREEN";
    case ScreenState::WIFI_SCREEN:
      return "WIFI_SCREEN";
    case ScreenState::SYNC_SCREEN:
      return "SYNC_SCREEN";
    case ScreenState::CALIBRATION_SCREEN:
      return "CALIBRATION_SCREEN";
    default:
      return "UNKNOWN";
  }
}

const char* gpsStateToString(GPSState state) {
  switch (state) {
    case GPSState::NOT_STARTED:
      return "NOT_STARTED";
    case GPSState::ACQUIRING:
      return "ACQUIRING";
    case GPSState::LOCKED:
      return "LOCKED";
    case GPSState::LOST:
      return "LOST";
    default:
      return "UNKNOWN";
  }
}

const char* recordingStateToString(RecordingState state) {
  switch (state) {
    case RecordingState::IDLE:
      return "IDLE";
    case RecordingState::STARTING:
      return "STARTING";
    case RecordingState::ACTIVE:
      return "ACTIVE";
    case RecordingState::STOPPING:
      return "STOPPING";
    case RecordingState::FAILED:
      return "FAILED";
    default:
      return "UNKNOWN";
  }
}

const char* wifiStateToString(WifiState state) {
  switch (state) {
    case WifiState::DISCONNECTED:
      return "DISCONNECTED";
    case WifiState::CONNECTING:
      return "CONNECTING";
    case WifiState::CONNECTED:
      return "CONNECTED";
    case WifiState::ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

const char* syncStateToString(SyncState state) {
  switch (state) {
    case SyncState::IDLE:
      return "IDLE";
    case SyncState::SYNCING:
      return "SYNCING";
    default:
      return "UNKNOWN";
  }
}

const char* recoveryStateToString(RecoveryState state) {
  switch (state) {
    case RecoveryState::IDLE:
      return "IDLE";
    case RecoveryState::NEEDED:
      return "NEEDED";
    default:
      return "UNKNOWN";
  }
}
