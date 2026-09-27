#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "events/app_controller.h"

// Helper to build an AppEvent
static AppEvent makeGPSDataEvent(double lat, double lng) {
  AppEvent e;
  e.type = EventType::GPS_DATA;
  e.data.gpsData = GPSData{};
  e.data.gpsData.lat = lat;
  e.data.gpsData.lng = lng;
  return e;
}

static AppEvent makeGPSStateEvent(GPSState state) {
  AppEvent e;
  e.type = EventType::GPS_STATE;
  e.data.gpsState = state;
  return e;
}

static AppEvent makeButtonEvent(ButtonId button) {
  AppEvent e;
  e.type = EventType::BUTTON_PRESS;
  e.data.buttonId = button;
  return e;
}

static AppEvent makeSyncCompleteEvent(int succeeded, int failed) {
  AppEvent e;
  e.type = EventType::SYNC_COMPLETE;
  e.data.syncResult = SyncResult{succeeded, failed};
  return e;
}

// The wire packet the transport would have handed over, built field by name
// because the aggregate order starts with version/type and those are the two
// fields nothing above the radio cares about.
static moana::Event wirePacket(uint16_t seq, uint8_t kind, uint8_t mode,
                               uint32_t utc,
                               uint16_t oldestUnacked = moana::kEventSeqNone) {
  moana::Event ev = {};
  ev.version = moana::kProtocolVersion;
  ev.type = moana::PACKET_EVENT;
  ev.seq = seq;
  ev.kind = kind;
  ev.mode = mode;
  ev.utc_seconds = utc;
  // kEventSeqNone by default: "the watch declined to say", which is the value
  // that leaves the frontier rules alone (issue #177).
  ev.oldest_unacked_seq = oldestUnacked;
  return ev;
}

// A mode-change arrival: one wire kind carrying the mode (issue #146).
static AppEvent makeModeEvent(uint16_t seq, uint8_t mode, uint32_t utc,
                              uint16_t oldestUnacked = moana::kEventSeqNone) {
  AppEvent e;
  e.type = EventType::MODE_EVENT;
  e.data.modeEvent =
      wirePacket(seq, moana::EVENT_MODE_CHANGE, mode, utc, oldestUnacked);
  return e;
}

// A non-mode (or otherwise unrecognised) arrival — kind and mode spelled out.
static AppEvent makeRawEvent(uint16_t seq, uint8_t kind, uint8_t mode,
                             uint32_t utc) {
  AppEvent e;
  e.type = EventType::MODE_EVENT;
  e.data.modeEvent = wirePacket(seq, kind, mode, utc);
  return e;
}

class AppControllerTest : public ::testing::Test {
 protected:
  AppController controller;

  // Tracking state
  std::vector<ScreenState> screensShown;
  int statusBarCalls = 0;
  GPSState lastStatusBarGPS = GPSState::NOT_STARTED;
  RecordingState lastStatusBarRecording = RecordingState::IDLE;
  std::vector<GPSData> displayGPSUpdates;
  std::vector<GPSData> recordingGPSFeeds;
  std::vector<GPSData> connectivityGPSReceives;
  int toggleRecordingCalls = 0;
  std::vector<bool> setRecordingCalls;
  struct AppendedEvent {
    uint8_t kind;
    uint8_t mode;
    uint32_t utc;
  };
  std::vector<AppendedEvent> appendedEvents;
  EventPersist appendEventResult = EventPersist::WRITTEN;
  int toggleConnectivityCalls = 0;
  SyncState lastSyncState = SyncState::IDLE;
  int syncUpdateCalls = 0;
  int lastSyncTrackCount = 0;
  int syncResultCalls = 0;
  SyncResult lastSyncResult{};
  int syncDeferredCalls = 0;
  std::string fatalError;
  std::vector<AppEvent> postedEvents;

  // Controllable state
  RecordingState recordingState = RecordingState::IDLE;
  ScreenState screenState = ScreenState::SYNC_SCREEN;
  std::vector<String> tracksOnLittleFS;
  bool syncStartResult = true;
  bool formatResult = true;
  int initLoggerCalls = 0;
  int markBootCleanCalls = 0;
  int formatCalls = 0;
  int imuStartCalls = 0;
  int imuCancelCalls = 0;
  int imuApplyCalls = 0;
  int imuDiscardCalls = 0;
  SyncCompleteFn capturedSyncCallback;

  void SetUp() override {
    AppController::Callbacks cb;

    cb.display.apply = [&](const DisplayCmd& cmd) {
      switch (cmd.kind) {
        case DisplayCmd::Kind::SHOW_SCREEN:
          screensShown.push_back(cmd.data.screen);
          break;
        case DisplayCmd::Kind::UPDATE_STATUS_BAR:
          statusBarCalls++;
          lastStatusBarGPS = cmd.data.statusBar.gps;
          lastStatusBarRecording = cmd.data.statusBar.recording;
          break;
        case DisplayCmd::Kind::UPDATE_GPS_DATA:
          displayGPSUpdates.push_back(cmd.data.gpsData);
          break;
        case DisplayCmd::Kind::UPDATE_SYNC:
          syncUpdateCalls++;
          lastSyncState = cmd.data.sync.state;
          lastSyncTrackCount = cmd.data.sync.trackCount;
          break;
        case DisplayCmd::Kind::SHOW_SYNC_RESULT:
          syncResultCalls++;
          lastSyncResult = cmd.data.syncResult;
          break;
        case DisplayCmd::Kind::SHOW_SYNC_DEFERRED:
          syncDeferredCalls++;
          break;
        case DisplayCmd::Kind::SHOW_FATAL_ERROR:
          fatalError = cmd.data.fatalMessage;
          break;
      }
    };
    cb.display.getScreenState = [&]() { return screenState; };

    cb.recording.toggle = [&]() { toggleRecordingCalls++; };
    cb.recording.setRecording = [&](bool on) { setRecordingCalls.push_back(on); };
    cb.recording.feedGPSData = [&](const GPSData& d) { recordingGPSFeeds.push_back(d); };
    cb.recording.getState = [&]() { return recordingState; };
    cb.recording.appendEvent = [&](uint8_t kind, uint8_t mode,
                                   uint32_t utc) -> EventPersist {
      appendedEvents.push_back({kind, mode, utc});
      return appendEventResult;
    };

    cb.connectivity.toggle = [&]() { toggleConnectivityCalls++; };
    cb.connectivity.receiveGPSData = [&](const GPSData& d) {
      connectivityGPSReceives.push_back(d);
    };

    cb.storage.localTracks = [&]() { return tracksOnLittleFS; };
    cb.storage.startSync = [&](bool deleteAfter, SyncCompleteFn completionCb) -> bool {
      capturedSyncCallback = std::move(completionCb);
      return syncStartResult;
    };
    cb.storage.formatLocal = [&]() -> bool {
      formatCalls++;
      return formatResult;
    };

    cb.logger.initLogger = [&]() -> bool {
      initLoggerCalls++;
      return true;
    };

    cb.imu.startCalibration = [&]() -> bool { imuStartCalls++; return true; };
    cb.imu.cancelCalibration = [&]() { imuCancelCalls++; };
    cb.imu.applyPendingCalibration = [&]() -> bool { imuApplyCalls++; return true; };
    cb.imu.discardPendingCalibration = [&]() { imuDiscardCalls++; };

    // Planar stub (1 degree = 1 metre) so distance expectations are exact;
    // production injects TinyGPSPlus::distanceBetween (issue #142).
    cb.distanceBetween = [](double lat1, double lng1, double lat2,
                            double lng2) {
      double dLat = lat2 - lat1;
      double dLng = lng2 - lng1;
      return std::sqrt(dLat * dLat + dLng * dLng);
    };

    cb.postEvent = [&](const AppEvent& e) { postedEvents.push_back(e); };
    cb.postEventCritical = [&](const AppEvent& e) { postedEvents.push_back(e); };
    cb.markBootClean = [&]() { markBootCleanCalls++; };

    controller.init(std::move(cb));
  }
};

// --- GPS data fan-out ---

TEST_F(AppControllerTest, GPSDataUpdatesAllSubsystems) {
  controller.process(makeGPSDataEvent(-36.8, 174.7));

  EXPECT_EQ(displayGPSUpdates.size(), 1u);
  EXPECT_DOUBLE_EQ(displayGPSUpdates[0].lat, -36.8);
  EXPECT_EQ(recordingGPSFeeds.size(), 1u);
  EXPECT_EQ(connectivityGPSReceives.size(), 1u);
}

TEST_F(AppControllerTest, GPSDataTransitionsStateToLocked) {
  controller.process(makeGPSDataEvent(-36.8, 174.7));

  EXPECT_EQ(controller.getGPSState(), GPSState::LOCKED);
  EXPECT_GE(statusBarCalls, 1);
  EXPECT_EQ(lastStatusBarGPS, GPSState::LOCKED);
}

// --- GPS state deduplication ---

TEST_F(AppControllerTest, DuplicateGPSStateDoesNotNotifyTwice) {
  controller.process(makeGPSStateEvent(GPSState::ACQUIRING));
  int callsAfterFirst = statusBarCalls;

  controller.process(makeGPSStateEvent(GPSState::ACQUIRING));
  EXPECT_EQ(statusBarCalls, callsAfterFirst);
}

TEST_F(AppControllerTest, DifferentGPSStatesEachNotify) {
  controller.process(makeGPSStateEvent(GPSState::ACQUIRING));
  controller.process(makeGPSStateEvent(GPSState::LOCKED));
  EXPECT_EQ(statusBarCalls, 2);
}

// --- Command toggles ---
//
// Navigation no longer flows through ButtonId — screens call ScreenContext
// directly (see test_screen_context.cpp). AppController only handles the
// remaining commands.

TEST_F(AppControllerTest, ToggleRecordingButtonTogglesRecording) {
  controller.process(makeButtonEvent(ButtonId::TOGGLE_RECORDING));
  EXPECT_EQ(toggleRecordingCalls, 1);
}

TEST_F(AppControllerTest, SetRecordingEventForwardsDesiredState) {
  // ESP-NOW commands from the watch enter through the same event queue and
  // reach the recorder as absolute set-on/set-off, never toggle.
  AppEvent on;
  on.type = EventType::SET_RECORDING;
  on.data.recordingDesired = true;
  controller.process(on);

  AppEvent off;
  off.type = EventType::SET_RECORDING;
  off.data.recordingDesired = false;
  controller.process(off);

  ASSERT_EQ(setRecordingCalls.size(), 2u);
  EXPECT_TRUE(setRecordingCalls[0]);
  EXPECT_FALSE(setRecordingCalls[1]);
  EXPECT_EQ(toggleRecordingCalls, 0);
}

TEST_F(AppControllerTest, WifiToggleButtonTogglesConnectivity) {
  controller.process(makeButtonEvent(ButtonId::TOGGLE_WIFI));
  EXPECT_EQ(toggleConnectivityCalls, 1);
}

TEST_F(AppControllerTest, FormatButtonFormatsLocalFilesystem) {
  controller.process(makeButtonEvent(ButtonId::FORMAT));
  EXPECT_EQ(formatCalls, 1);
}

// R10: formatting wipes LittleFS — must be refused while the recorder holds a
// track file open or a sync is reading local tracks.
TEST_F(AppControllerTest, FormatBlockedWhileRecording) {
  recordingState = RecordingState::ACTIVE;
  controller.process(makeButtonEvent(ButtonId::FORMAT));
  EXPECT_EQ(formatCalls, 0);
}

TEST_F(AppControllerTest, FormatBlockedWhileSyncing) {
  // START_SYNC sets syncState=SYNCING (screen defaults to SYNC_SCREEN, idle).
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  ASSERT_EQ(controller.getSyncState(), SyncState::SYNCING);
  controller.process(makeButtonEvent(ButtonId::FORMAT));
  EXPECT_EQ(formatCalls, 0);
}

// --- IMU calibration buttons ---

TEST_F(AppControllerTest, StartCalibrationForwardsWhenRecordingIdle) {
  recordingState = RecordingState::IDLE;
  controller.process(makeButtonEvent(ButtonId::START_IMU_CALIBRATION));
  EXPECT_EQ(imuStartCalls, 1);
}

TEST_F(AppControllerTest, StartCalibrationBlockedWhenRecordingActive) {
  recordingState = RecordingState::ACTIVE;
  controller.process(makeButtonEvent(ButtonId::START_IMU_CALIBRATION));
  EXPECT_EQ(imuStartCalls, 0);
}

TEST_F(AppControllerTest, CancelApplyDiscardCalibrationForward) {
  controller.process(makeButtonEvent(ButtonId::CANCEL_IMU_CALIBRATION));
  controller.process(makeButtonEvent(ButtonId::APPLY_IMU_CALIBRATION));
  controller.process(makeButtonEvent(ButtonId::DISCARD_IMU_CALIBRATION));
  EXPECT_EQ(imuCancelCalls, 1);
  EXPECT_EQ(imuApplyCalls, 1);
  EXPECT_EQ(imuDiscardCalls, 1);
}

// --- Sync guard conditions ---

TEST_F(AppControllerTest, SyncStartsUnderCorrectConditions) {
  // Default fixture: IDLE sync, SYNC_SCREEN, recording IDLE
  tracksOnLittleFS = {String("track1.csv"), String("track2.csv")};
  controller.process(makeButtonEvent(ButtonId::START_SYNC));

  EXPECT_EQ(controller.getSyncState(), SyncState::SYNCING);
  EXPECT_GE(syncUpdateCalls, 1);
  EXPECT_EQ(lastSyncState, SyncState::SYNCING);
  EXPECT_EQ(lastSyncTrackCount, 2);
}

TEST_F(AppControllerTest, SyncBlockedWhenNotOnSyncScreen) {
  screenState = ScreenState::MAIN_MENU;
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  EXPECT_EQ(controller.getSyncState(), SyncState::IDLE);
}

TEST_F(AppControllerTest, SyncBlockedWhenRecordingActive) {
  recordingState = RecordingState::ACTIVE;
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  EXPECT_EQ(controller.getSyncState(), SyncState::IDLE);
}

TEST_F(AppControllerTest, SyncBlockedWhenAlreadySyncing) {
  // Start first sync
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  EXPECT_EQ(controller.getSyncState(), SyncState::SYNCING);

  // Attempt second sync
  int callsBefore = syncUpdateCalls;
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  // No additional sync update — second request ignored
  EXPECT_EQ(syncUpdateCalls, callsBefore);
}

TEST_F(AppControllerTest, SyncFailureToStartResetsState) {
  syncStartResult = false;
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  EXPECT_EQ(controller.getSyncState(), SyncState::IDLE);
  // The refusal is surfaced on-screen (archive busy — try again), not silently
  // snapped back to a plain idle state.
  EXPECT_EQ(syncDeferredCalls, 1);
}

// --- Sync completion ---

TEST_F(AppControllerTest, SyncCompletionResetsToIdle) {
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  ASSERT_TRUE(capturedSyncCallback);

  // Simulate sync complete via the captured callback posting an event
  controller.process(makeSyncCompleteEvent(3, 0));
  EXPECT_EQ(controller.getSyncState(), SyncState::IDLE);
}

// R3: the storage completion callback must post a SYNC_COMPLETE back to the
// queue (via the guaranteed-delivery path) — otherwise the state machine never
// leaves SYNCING.
TEST_F(AppControllerTest, SyncCompletionCallbackPostsSyncCompleteEvent) {
  controller.process(makeButtonEvent(ButtonId::START_SYNC));
  ASSERT_TRUE(capturedSyncCallback);

  SyncResult result{};
  result.filesSucceeded = 3;
  capturedSyncCallback(result);

  ASSERT_FALSE(postedEvents.empty());
  EXPECT_EQ(postedEvents.back().type, EventType::SYNC_COMPLETE);
  EXPECT_EQ(postedEvents.back().data.syncResult.filesSucceeded, 3);
}

// --- Recovery flow ---

TEST_F(AppControllerTest, RecoveryBypassesSyncGuards) {
  screenState = ScreenState::MAIN_MENU;
  recordingState = RecordingState::ACTIVE;

  controller.setRecoveryState(RecoveryState::NEEDED);
  controller.startSync();

  EXPECT_EQ(controller.getSyncState(), SyncState::SYNCING);
}

TEST_F(AppControllerTest, RecoveryCompletesFullRecoveryFlow) {
  controller.setRecoveryState(RecoveryState::NEEDED);
  controller.startSync();

  // Simulate sync complete
  controller.process(makeSyncCompleteEvent(2, 0));

  EXPECT_EQ(controller.getRecoveryState(), RecoveryState::IDLE);
  EXPECT_EQ(formatCalls, 1);
  EXPECT_EQ(initLoggerCalls, 1);
  EXPECT_EQ(markBootCleanCalls, 1);
  ASSERT_FALSE(screensShown.empty());
  EXPECT_EQ(screensShown.back(), ScreenState::MAIN_MENU);
}

TEST_F(AppControllerTest, RecoveryFormatFailureShowsFatalError) {
  formatResult = false;
  controller.setRecoveryState(RecoveryState::NEEDED);
  controller.startSync();

  controller.process(makeSyncCompleteEvent(1, 0));

  EXPECT_FALSE(fatalError.empty());
  // Logger and boot clean should NOT be called after format failure
  EXPECT_EQ(initLoggerCalls, 0);
  EXPECT_EQ(markBootCleanCalls, 0);
}

// --- Watch mode events (issue #139) ---
//
// The seq-gap / dedupe mechanics live in EventIngest (test_event_ingest.cpp);
// these tests cover AppController's side: applying the mode echo, advancing
// the telemetry ack frontier, and persisting through the recording callback.

TEST_F(AppControllerTest, ModeEventUpdatesModeAppendsAndAcks) {
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 1'753'900'000));

  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_SAIL);
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
  ASSERT_EQ(appendedEvents.size(), 1u);
  EXPECT_EQ(appendedEvents[0].kind, moana::EVENT_MODE_CHANGE);
  EXPECT_EQ(appendedEvents[0].mode, moana::MODE_SAIL);
  // Sidecar carries the press-time UTC from the packet, not receive time.
  EXPECT_EQ(appendedEvents[0].utc, 1'753'900'000u);
}

TEST_F(AppControllerTest, EveryModeEchoesAndReachesTheSidecar) {
  // The watch picker can send any of the ten server modes (issue #146) — the
  // echo and the sidecar must accept all of them, not just the button three.
  const uint8_t kAll[] = {
      moana::MODE_SAIL,      moana::MODE_MOTORSAIL, moana::MODE_MOTOR,
      moana::MODE_ROW,       moana::MODE_UNPOWERED, moana::MODE_ANCHOR,
      moana::MODE_ALONGSIDE, moana::MODE_BEACHED,   moana::MODE_TRAILERED,
      moana::MODE_OTHER,
  };
  const size_t n = sizeof(kAll) / sizeof(kAll[0]);

  for (size_t i = 0; i < n; i++) {
    controller.process(makeModeEvent((uint16_t)(i + 1), kAll[i], 1000 + i));
    EXPECT_EQ(controller.getCurrentMode(), kAll[i]);
  }

  ASSERT_EQ(appendedEvents.size(), n);
  for (size_t i = 0; i < n; i++) {
    EXPECT_EQ(appendedEvents[i].mode, kAll[i]);
    EXPECT_EQ(appendedEvents[i].utc, 1000u + i);
  }
  EXPECT_EQ(controller.getEventAckFrontier(), n);
}

TEST_F(AppControllerTest, ModeStartsUnknownWithNoEventsAcked) {
  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_UNKNOWN);
  EXPECT_EQ(controller.getEventAckFrontier(), moana::kEventSeqNone);
}

TEST_F(AppControllerTest, DuplicateModeEventIsNotAppendedTwice) {
  controller.process(makeModeEvent(1, moana::MODE_MOTOR, 100));
  controller.process(makeModeEvent(1, moana::MODE_MOTOR, 100));

  EXPECT_EQ(appendedEvents.size(), 1u);
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
}

TEST_F(AppControllerTest, SeqGapHoldsAckThenAppliesBothInOrder) {
  // Drop event 2, deliver 3: ack holds at 1 and nothing new is persisted.
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));
  controller.process(makeModeEvent(3, moana::MODE_OTHER, 300));
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_SAIL);
  EXPECT_EQ(appendedEvents.size(), 1u);

  // The watch resends both; 2 fills the hole -> 2 and 3 apply in order,
  // exactly once each (no duplicates in the sidecar).
  controller.process(makeModeEvent(2, moana::MODE_MOTOR, 200));
  controller.process(makeModeEvent(3, moana::MODE_OTHER, 300));

  EXPECT_EQ(controller.getEventAckFrontier(), 3);
  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_OTHER);
  ASSERT_EQ(appendedEvents.size(), 3u);
  EXPECT_EQ(appendedEvents[1].utc, 200u);
  EXPECT_EQ(appendedEvents[2].utc, 300u);
}

TEST_F(AppControllerTest, ModeEventWhileNotRecordingStillEchoesAndAcks) {
  // Policy: no active session -> the event shapes the current_mode echo and
  // is acked (the watch must stop resending), it just isn't persisted.
  appendEventResult = EventPersist::NO_SESSION;
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));

  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_SAIL);
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
}

// --- A sidecar that won't take the line (issue #180) ---
//
// The distinction the whole fix turns on: "not recording" is a deliberate ack,
// "the write failed" must not be. Acking a failed write has the watch trim the
// event from NVS while the server never sees it — lost at both ends, silently.

TEST_F(AppControllerTest, FailedSidecarWriteDoesNotAdvanceTheAckFrontier) {
  appendEventResult = EventPersist::FAILED;
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));

  // Echoed — the boat did apply it, and the wrist should agree with the boat.
  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_SAIL);
  // But not acked: the watch keeps this one in NVS and keeps offering it.
  EXPECT_EQ(controller.getEventAckFrontier(), moana::kEventSeqNone);
}

TEST_F(AppControllerTest, TheResendAfterAFailedWriteIsRetriedNotIgnored) {
  // The half that makes the hold worth anything. Holding the *published*
  // frontier alone would deadlock: EventIngest would treat the resend as a
  // duplicate and never offer it to the sidecar again.
  appendEventResult = EventPersist::FAILED;
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));
  ASSERT_EQ(appendedEvents.size(), 1u);

  appendEventResult = EventPersist::WRITTEN;  // filesystem recovers
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));

  ASSERT_EQ(appendedEvents.size(), 2u);
  EXPECT_EQ(appendedEvents[1].utc, 100u);
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
}

TEST_F(AppControllerTest, AFailedWriteKeepsTheAckEarnedByTheEventsBeforeIt) {
  // A batch drains in seq order, so a failure part way through must keep what
  // landed and hold only from there — re-offering the whole batch would write
  // the earlier events twice.
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));
  ASSERT_EQ(controller.getEventAckFrontier(), 1);

  // 3 arrives first and buffers; 2 then fills the hole, so 2 and 3 drain
  // together — with the sidecar refusing both.
  controller.process(makeModeEvent(3, moana::MODE_OTHER, 300));
  appendEventResult = EventPersist::FAILED;
  controller.process(makeModeEvent(2, moana::MODE_MOTOR, 200));

  EXPECT_EQ(controller.getEventAckFrontier(), 1);
  EXPECT_EQ(appendedEvents.size(), 2u);  // 1 written, 2 attempted and refused
}

TEST_F(AppControllerTest, AFailureStopsTheBatchRatherThanSkippingPastIt) {
  // Writing 3 while 2 was refused would put the sidecar out of order and ack
  // over a mode change that never landed. The batch stops at the refusal.
  controller.process(makeModeEvent(2, moana::MODE_OTHER, 200));  // buffers
  appendEventResult = EventPersist::FAILED;
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));

  ASSERT_EQ(appendedEvents.size(), 1u);
  EXPECT_EQ(appendedEvents[0].utc, 100u);  // 2 was never attempted
  EXPECT_EQ(controller.getEventAckFrontier(), moana::kEventSeqNone);

  // Both come back around on the watch's next resend and land in order.
  appendEventResult = EventPersist::WRITTEN;
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));
  controller.process(makeModeEvent(2, moana::MODE_OTHER, 200));

  ASSERT_EQ(appendedEvents.size(), 3u);
  EXPECT_EQ(appendedEvents[1].utc, 100u);
  EXPECT_EQ(appendedEvents[2].utc, 200u);
  EXPECT_EQ(controller.getEventAckFrontier(), 2);
}

TEST_F(AppControllerTest, ADeadRangeTheIngestJumpedIsPublishedAsAcked) {
  // The frontier the watch reads has to include a range nothing was applied
  // for: seq 2 was dropped by the watch and will never be offered again, so
  // an ack that stops at 1 has the watch resending 3 for ever (issue #177).
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100, 1));
  ASSERT_EQ(controller.getEventAckFrontier(), 1);

  controller.process(makeModeEvent(3, moana::MODE_MOTOR, 300, 3));
  EXPECT_EQ(controller.getEventAckFrontier(), 3);
}

TEST_F(AppControllerTest, AnEventRecoveredFromBehindTheJumpReachesTheSidecar) {
  // Seq 3 arrived and buffered while seq 2 still looked live; the watch then
  // dropped both 2 and 4, so the frontier jumps to 5 — over a seq the boat is
  // holding. It gets written on the way past rather than binned.
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100, 1));
  controller.process(makeModeEvent(3, moana::MODE_MOTOR, 300, 2));
  ASSERT_EQ(appendedEvents.size(), 1u);

  controller.process(makeModeEvent(5, moana::MODE_ANCHOR, 500, 5));

  ASSERT_EQ(appendedEvents.size(), 3u);
  EXPECT_EQ(appendedEvents[1].utc, 300u);  // recovered, and in seq order
  EXPECT_EQ(appendedEvents[2].utc, 500u);
  EXPECT_EQ(controller.getEventAckFrontier(), 5);
}

TEST_F(AppControllerTest, ARefusedRecoveredEventStillHoldsTheFrontier) {
  // A refusal stops the batch wherever it happens (issue #180). The retry it
  // buys cannot reach *this* event — the watch dropped seq 3 long ago — but
  // the hold is still right for everything behind it, and the next burst
  // re-declares the same dead range, so delivery of 5 is not stuck either.
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100, 1));
  controller.process(makeModeEvent(3, moana::MODE_MOTOR, 300, 2));

  appendEventResult = EventPersist::FAILED;
  controller.process(makeModeEvent(5, moana::MODE_ANCHOR, 500, 5));

  EXPECT_EQ(appendedEvents.size(), 2u);  // 1 written, 3 attempted and refused
  EXPECT_EQ(controller.getEventAckFrontier(), 1);

  appendEventResult = EventPersist::WRITTEN;  // filesystem recovers
  controller.process(makeModeEvent(5, moana::MODE_ANCHOR, 500, 5));

  ASSERT_EQ(appendedEvents.size(), 3u);
  EXPECT_EQ(appendedEvents[2].utc, 500u);  // 3 is gone; 5 lands
  EXPECT_EQ(controller.getEventAckFrontier(), 5);
}

TEST_F(AppControllerTest, AnUnhandledKindIsStillAckedThroughAFailingSidecar) {
  // An event this firmware can't apply never reaches the sidecar, so the
  // sidecar's health has nothing to do with it — it must be acked either way,
  // or a newer watch's event kind wedges delivery for everything behind it.
  appendEventResult = EventPersist::FAILED;
  AppEvent event = makeModeEvent(1, moana::MODE_UNKNOWN, 100);
  controller.process(event);

  EXPECT_TRUE(appendedEvents.empty());
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
}

// --- Cumulative session distance (issue #142) ---
//
// The when-to-integrate rules live in DistanceAccumulator
// (test_distance_accumulator.cpp); these tests cover AppController's
// wiring: GPS_DATA feeds it under the live recording state, GPS dropouts
// break the anchor, and the telemetry getter exposes the total.

TEST_F(AppControllerTest, GPSDataAccumulatesDistanceWhileRecordingActive) {
  recordingState = RecordingState::ACTIVE;
  controller.process(makeGPSDataEvent(0, 0));
  controller.process(makeGPSDataEvent(0, 3));
  controller.process(makeGPSDataEvent(4, 3));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 7u);
}

TEST_F(AppControllerTest, GPSDataDoesNotAccumulateWhileIdle) {
  recordingState = RecordingState::IDLE;
  controller.process(makeGPSDataEvent(0, 0));
  controller.process(makeGPSDataEvent(0, 100));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 0u);
}

TEST_F(AppControllerTest, DistanceHeldAfterRecordingStopsAndResetOnRestart) {
  recordingState = RecordingState::ACTIVE;
  controller.process(makeGPSDataEvent(0, 0));
  controller.process(makeGPSDataEvent(0, 5));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 5u);

  recordingState = RecordingState::IDLE;
  controller.process(makeGPSDataEvent(0, 50));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 5u);  // held after stop

  recordingState = RecordingState::ACTIVE;
  controller.process(makeGPSDataEvent(0, 60));  // new session: reset + anchor
  EXPECT_EQ(controller.getCumulativeDistanceM(), 0u);
  controller.process(makeGPSDataEvent(0, 62));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 2u);
}

TEST_F(AppControllerTest, GPSStateLossBreaksDistanceAnchor) {
  recordingState = RecordingState::ACTIVE;
  controller.process(makeGPSDataEvent(0, 0));
  controller.process(makeGPSDataEvent(0, 3));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 3u);

  // Fix lost, boat travels 100 m blind, fix reacquired: the straight-line
  // jump across the gap is not integrated.
  controller.process(makeGPSStateEvent(GPSState::LOST));
  controller.process(makeGPSDataEvent(0, 103));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 3u);
  controller.process(makeGPSDataEvent(0, 105));
  EXPECT_EQ(controller.getCumulativeDistanceM(), 5u);
}

TEST_F(AppControllerTest, UnknownEventKindAckedButNotApplied) {
  // Future kinds (e.g. voice markers) must not disturb the mode echo, but
  // still ack so the watch stops resending them.
  controller.process(
      makeRawEvent(1, moana::EVENT_VOICE_MARKER, moana::MODE_UNKNOWN, 100));

  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_UNKNOWN);
  EXPECT_EQ(controller.getEventAckFrontier(), 1);
  EXPECT_TRUE(appendedEvents.empty());
}

TEST_F(AppControllerTest, UnknownModeValueAckedButNotApplied) {
  // A mode from a newer watch firmware: the echo must not adopt a value this
  // firmware can't name, but the event is still acked (issue #146).
  controller.process(makeModeEvent(1, moana::MODE_SAIL, 100));
  controller.process(makeRawEvent(2, moana::EVENT_MODE_CHANGE, 200, 200));

  EXPECT_EQ(controller.getCurrentMode(), moana::MODE_SAIL);
  EXPECT_EQ(controller.getEventAckFrontier(), 2);
  EXPECT_EQ(appendedEvents.size(), 1u);
}
