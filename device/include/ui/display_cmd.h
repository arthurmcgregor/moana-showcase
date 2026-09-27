#pragma once

// DisplayCmd — tagged union of every render-side instruction AppController
// can issue. Collapses the seven-field DisplayCallbacks lambda struct into
// a single Display::apply(const DisplayCmd&) entry point.

#include <cstring>

#include "hardware/gps_types.h"
#include "storage/sync_types.h"
#include "types.h"

struct DisplayCmd {
  enum class Kind {
    SHOW_SCREEN,
    UPDATE_STATUS_BAR,
    UPDATE_GPS_DATA,
    UPDATE_SYNC,
    SHOW_SYNC_RESULT,
    SHOW_SYNC_DEFERRED,
    SHOW_FATAL_ERROR,
  };

  struct StatusBarPayload {
    GPSState gps;
    RecordingState recording;
  };

  struct SyncPayload {
    SyncState state;
    int trackCount;
  };

  Kind kind;
  union Data {
    ScreenState screen;
    StatusBarPayload statusBar;
    GPSData gpsData;
    SyncPayload sync;
    SyncResult syncResult;
    const char* fatalMessage;
    Data() { memset(this, 0, sizeof(Data)); }
  } data;

  static DisplayCmd showScreen(ScreenState s) {
    DisplayCmd c{};
    c.kind = Kind::SHOW_SCREEN;
    c.data.screen = s;
    return c;
  }

  static DisplayCmd updateStatusBar(GPSState gps, RecordingState rec) {
    DisplayCmd c{};
    c.kind = Kind::UPDATE_STATUS_BAR;
    c.data.statusBar = {gps, rec};
    return c;
  }

  static DisplayCmd updateGPSData(const GPSData& d) {
    DisplayCmd c{};
    c.kind = Kind::UPDATE_GPS_DATA;
    c.data.gpsData = d;
    return c;
  }

  static DisplayCmd updateSync(SyncState state, int trackCount) {
    DisplayCmd c{};
    c.kind = Kind::UPDATE_SYNC;
    c.data.sync = {state, trackCount};
    return c;
  }

  static DisplayCmd showSyncResult(const SyncResult& r) {
    DisplayCmd c{};
    c.kind = Kind::SHOW_SYNC_RESULT;
    c.data.syncResult = r;
    return c;
  }

  // Sync couldn't start because the SD mount is held by a web archive op —
  // surface it so the user knows to retry (no payload; state stays IDLE).
  static DisplayCmd showSyncDeferred() {
    DisplayCmd c{};
    c.kind = Kind::SHOW_SYNC_DEFERRED;
    return c;
  }

  static DisplayCmd showFatalError(const char* msg) {
    DisplayCmd c{};
    c.kind = Kind::SHOW_FATAL_ERROR;
    c.data.fatalMessage = msg;
    return c;
  }
};
