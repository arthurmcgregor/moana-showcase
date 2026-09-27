#include "events/app_controller.h"

#include "utils/logger.h"
#include "utils/state_strings.h"

static const char* LOG_TAG = "AppController";

void AppController::init(Callbacks callbacks) {
  cb = std::move(callbacks);
  distance.init(cb.distanceBetween);
}

void AppController::process(const AppEvent& event) {
  switch (event.type) {
    case EventType::GPS_STATE:
      transitionGPS(event.data.gpsState);
      break;

    case EventType::GPS_DATA:
      transitionGPS(GPSState::LOCKED);
      cb.display.apply(DisplayCmd::updateGPSData(event.data.gpsData));
      cb.recording.feedGPSData(event.data.gpsData);
      cb.connectivity.receiveGPSData(event.data.gpsData);
      // Session distance echo (issue #142): GPS_DATA only fires on valid
      // fixes, so every event here may integrate; invalid gaps are signalled
      // through transitionGPS below.
      distance.onFix(event.data.gpsData.lat, event.data.gpsData.lng,
                     cb.recording.getState() == RecordingState::ACTIVE);
      break;

    case EventType::BUTTON_PRESS:
      handleButtonPress(event.data.buttonId);
      break;

    case EventType::SYNC_COMPLETE:
      onSyncComplete(event.data.syncResult);
      break;

    case EventType::SET_RECORDING:
      LOG_INFO(LOG_TAG, "Remote SET_RECORDING: %s",
               event.data.recordingDesired ? "on" : "off");
      cb.recording.setRecording(event.data.recordingDesired);
      break;

    case EventType::MODE_EVENT:
      handleModeEvent(event.data.modeEvent);
      break;
  }
}

void AppController::handleModeEvent(const moana::Event& event) {
  // Dedupe + contiguous-ack bookkeeping (issue #139): duplicates and
  // out-of-order arrivals return nothing to apply; the frontier only ever
  // advances over a gap once the missing event has arrived.
  std::vector<moana::Event> applied = eventIngest.accept(event);

  // The frontier is published only for what this loop actually settles
  // (issue #180). It starts where the last batch left it and follows each
  // event that either reached the sidecar or had no session to reach, so an
  // event the sidecar refused is never acked: acking it would have the watch
  // trim it from NVS while the server never sees it, and nothing anywhere
  // would say so.
  uint16_t settled = eventAckFrontier;

  for (const moana::Event& e : applied) {
    uint8_t mode = moana::sailingModeForEvent(e.kind, e.mode);
    if (mode == moana::MODE_UNKNOWN) {
      // Non-mode kind (future slices) or a mode value this firmware doesn't
      // know (a newer watch): acked so the watch stops resending, but nothing
      // to apply here.
      LOG_WARN(LOG_TAG, "Watch event kind %u mode %u not handled — acked only",
               e.kind, e.mode);
      settled = e.seq;
      continue;
    }
    currentMode = mode;

    switch (cb.recording.appendEvent(e.kind, mode, e.utc_seconds)) {
      case EventPersist::WRITTEN:
        break;
      case EventPersist::NO_SESSION:
        // Deliberate: with no track there is nothing for the server ingest to
        // attach the event to, and an orphan events file would have no upload
        // vehicle. The mode echo has it, and the watch can stop resending.
        LOG_INFO(LOG_TAG,
                 "Mode event (%s) applied while not recording — echoed, "
                 "not persisted",
                 moana::sailingModeName(mode));
        break;
      case EventPersist::FAILED:
        // The sidecar was there and would not take the line. Stop here and
        // leave the frontier below this event: the watch resends every unacked
        // event once a second, so it comes back around and this retries until
        // the filesystem recovers or the queue fills. A full queue then starts
        // dropping its oldest (issue #177) — visible on the watch's Control
        // tile as a depth that only grows, which is a great deal better than
        // the silence this replaces.
        //
        // The one event this retry cannot reach is one recovered from behind a
        // frontier jump (issue #177): the watch has already dropped it, so a
        // refusal there is the end of it. The hold still costs nothing — the
        // next burst re-jumps the same dead range — and `Recorder::appendEvent`
        // has logged the I/O failure itself.
        LOG_ERROR(LOG_TAG,
                  "Sidecar write failed for seq %u (%s) — holding the ack "
                  "frontier at %u so the watch resends",
                  (unsigned)e.seq, moana::sailingModeName(mode),
                  (unsigned)settled);
        eventIngest.holdFrontierAt(settled);
        eventAckFrontier = settled;
        return;
    }
    settled = e.seq;
  }

  // Nothing refused, so everything the ingest has stepped past is genuinely
  // settled — including a dead range it jumped over, which is the whole point
  // of the jump (issue #177) and which no applied event's seq would reach.
  // The FAILED path above returns before this, which is what keeps #180's
  // rule intact: a refused event is never acked.
  eventAckFrontier = eventIngest.ackFrontier();
}

void AppController::transitionGPS(GPSState newState) {
  if (gpsState == newState) {
    return;
  }

  LOG_INFO(LOG_TAG, "GPS transition: %s -> %s", gpsStateToString(gpsState),
           gpsStateToString(newState));

  gpsState = newState;

  // Leaving LOCKED means the valid-fix stream broke: drop the distance
  // anchor so no straight-line segment is integrated across the dropout —
  // accumulation resumes from the next valid fix (issue #142).
  if (newState != GPSState::LOCKED) {
    distance.onFixLost();
  }

  cb.display.apply(DisplayCmd::updateStatusBar(newState, cb.recording.getState()));
}

void AppController::handleButtonPress(ButtonId buttonId) {
  switch (buttonId) {
    case ButtonId::START_SYNC:
      startSync();
      break;
    case ButtonId::TOGGLE_WIFI:
      cb.connectivity.toggle();
      break;
    case ButtonId::TOGGLE_RECORDING:
      cb.recording.toggle();
      break;
    case ButtonId::FORMAT:
      // Guard the destructive format: never wipe LittleFS while the recorder
      // holds a track file open, or while a sync is reading local tracks (R10).
      // Currently unreachable from any screen, but a landmine for the next menu
      // entry that wires it up.
      if (cb.recording.getState() != RecordingState::IDLE) {
        LOG_WARN(LOG_TAG, "Format blocked — recording active");
        break;
      }
      if (syncState != SyncState::IDLE) {
        LOG_WARN(LOG_TAG, "Format blocked — sync in progress");
        break;
      }
      cb.storage.formatLocal();
      break;
    case ButtonId::START_IMU_CALIBRATION:
      // Calibration freezes the fusion filter for 90 s. Block it while
      // recording is active so no CSV row gets stale IMU values.
      if (cb.recording.getState() != RecordingState::IDLE) {
        LOG_WARN(LOG_TAG, "Calibration blocked — recording active");
        break;
      }
      cb.imu.startCalibration();
      break;
    case ButtonId::CANCEL_IMU_CALIBRATION:
      cb.imu.cancelCalibration();
      break;
    case ButtonId::APPLY_IMU_CALIBRATION:
      cb.imu.applyPendingCalibration();
      break;
    case ButtonId::DISCARD_IMU_CALIBRATION:
      cb.imu.discardPendingCalibration();
      break;
  }
}

void AppController::startSync() {
  bool isRecovery = (recoveryState == RecoveryState::NEEDED);

  if (!isRecovery) {
    if (syncState != SyncState::IDLE) return;
    if (cb.display.getScreenState() != ScreenState::SYNC_SCREEN) return;
    if (cb.recording.getState() != RecordingState::IDLE) return;
  }

  bool deleteAfterSync = !isRecovery;
  syncState = SyncState::SYNCING;
  int trackCount = cb.storage.localTracks().size();
  cb.display.apply(DisplayCmd::updateSync(SyncState::SYNCING, trackCount));

  bool started = cb.storage.startSync(deleteAfterSync, [this](const SyncResult& result) {
    AppEvent event;
    event.type = EventType::SYNC_COMPLETE;
    event.data.syncResult = result;
    // Guaranteed delivery: a dropped SYNC_COMPLETE would strand us in SYNCING
    // forever, refusing all future syncs (R3).
    cb.postEventCritical(event);
  });

  if (!started) {
    // Storage refused: either a sync is already running or a web archive op
    // holds the mount (it logs which). Don't assert "already in progress" here
    // — that was misleading in the archive-busy-deferral case.
    LOG_WARN(LOG_TAG, "Sync did not start — archive busy (see storage log)");
    syncState = SyncState::IDLE;
    // Surface the deferral on-screen so the user knows to retry, rather than
    // silently snapping the button back to a plain idle state.
    cb.display.apply(DisplayCmd::showSyncDeferred());
  }
}

void AppController::onSyncComplete(const SyncResult& result) {
  syncState = SyncState::IDLE;
  int trackCount = cb.storage.localTracks().size();
  cb.display.apply(DisplayCmd::updateSync(SyncState::IDLE, trackCount));
  cb.display.apply(DisplayCmd::showSyncResult(result));

  if (recoveryState == RecoveryState::NEEDED) {
    if (result.filesFailed > 0) {
      LOG_ERROR(LOG_TAG,
                "Recovery aborted — %d file(s) failed to sync to SD",
                result.filesFailed);
      return;
    }
    recoveryState = RecoveryState::IDLE;
    LOG_INFO(LOG_TAG, "Recovery: formatting LittleFS...");
    if (!cb.storage.formatLocal()) {
      LOG_ERROR(LOG_TAG, "Recovery failed — LittleFS remount failed after format");
      cb.display.apply(DisplayCmd::showFatalError("RECOVERY FAILED\nFlash clear required"));
      return;
    }
    cb.logger.initLogger();
    cb.markBootClean();
    LOG_INFO(LOG_TAG, "Recovery complete — filesystem reformatted");
    cb.display.apply(DisplayCmd::showScreen(ScreenState::MAIN_MENU));
  }
}
