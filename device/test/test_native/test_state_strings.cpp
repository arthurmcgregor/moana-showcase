#include <gtest/gtest.h>

#include <cstring>

#include "utils/state_strings.h"

TEST(StateStrings, ScreenStateToString) {
  EXPECT_STREQ(screenStateToString(ScreenState::MAIN_MENU), "MAIN_MENU");
  EXPECT_STREQ(screenStateToString(ScreenState::SPEED_SCREEN), "SPEED_SCREEN");
  EXPECT_STREQ(screenStateToString(ScreenState::DETAIL_SCREEN), "DETAIL_SCREEN");
  EXPECT_STREQ(screenStateToString(ScreenState::WIFI_SCREEN), "WIFI_SCREEN");
  EXPECT_STREQ(screenStateToString(ScreenState::SYNC_SCREEN), "SYNC_SCREEN");
  EXPECT_STREQ(screenStateToString(ScreenState::CALIBRATION_SCREEN), "CALIBRATION_SCREEN");
}

TEST(StateStrings, GPSStateToString) {
  EXPECT_STREQ(gpsStateToString(GPSState::NOT_STARTED), "NOT_STARTED");
  EXPECT_STREQ(gpsStateToString(GPSState::ACQUIRING), "ACQUIRING");
  EXPECT_STREQ(gpsStateToString(GPSState::LOCKED), "LOCKED");
  EXPECT_STREQ(gpsStateToString(GPSState::LOST), "LOST");
}

TEST(StateStrings, RecordingStateToString) {
  EXPECT_STREQ(recordingStateToString(RecordingState::IDLE), "IDLE");
  EXPECT_STREQ(recordingStateToString(RecordingState::STARTING), "STARTING");
  EXPECT_STREQ(recordingStateToString(RecordingState::ACTIVE), "ACTIVE");
  EXPECT_STREQ(recordingStateToString(RecordingState::STOPPING), "STOPPING");
  EXPECT_STREQ(recordingStateToString(RecordingState::FAILED), "FAILED");
}

TEST(StateStrings, WifiStateToString) {
  EXPECT_STREQ(wifiStateToString(WifiState::DISCONNECTED), "DISCONNECTED");
  EXPECT_STREQ(wifiStateToString(WifiState::CONNECTING), "CONNECTING");
  EXPECT_STREQ(wifiStateToString(WifiState::CONNECTED), "CONNECTED");
  EXPECT_STREQ(wifiStateToString(WifiState::ERROR), "ERROR");
}

TEST(StateStrings, SyncStateToString) {
  EXPECT_STREQ(syncStateToString(SyncState::IDLE), "IDLE");
  EXPECT_STREQ(syncStateToString(SyncState::SYNCING), "SYNCING");
}

TEST(StateStrings, RecoveryStateToString) {
  EXPECT_STREQ(recoveryStateToString(RecoveryState::IDLE), "IDLE");
  EXPECT_STREQ(recoveryStateToString(RecoveryState::NEEDED), "NEEDED");
}

TEST(StateStrings, UnknownValuesReturnUnknown) {
  EXPECT_STREQ(screenStateToString(static_cast<ScreenState>(99)), "UNKNOWN");
  EXPECT_STREQ(gpsStateToString(static_cast<GPSState>(99)), "UNKNOWN");
  EXPECT_STREQ(recordingStateToString(static_cast<RecordingState>(99)), "UNKNOWN");
  EXPECT_STREQ(wifiStateToString(static_cast<WifiState>(99)), "UNKNOWN");
  EXPECT_STREQ(syncStateToString(static_cast<SyncState>(99)), "UNKNOWN");
  EXPECT_STREQ(recoveryStateToString(static_cast<RecoveryState>(99)), "UNKNOWN");
}
