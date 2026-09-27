#pragma once

#include "types.h"

// Helper functions to convert state enums to readable strings for logging

const char* screenStateToString(ScreenState state);
const char* gpsStateToString(GPSState state);
const char* recordingStateToString(RecordingState state);
const char* wifiStateToString(WifiState state);
const char* syncStateToString(SyncState state);
const char* recoveryStateToString(RecoveryState state);
