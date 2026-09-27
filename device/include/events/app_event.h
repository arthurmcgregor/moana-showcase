#pragma once

// Event types for inter-task communication — no FreeRTOS dependencies.
// Extracted so modules that only need AppEvent don't pull in the full App class.

#include <cstring>

#include "espnow_protocol.h"
#include "hardware/gps_types.h"
#include "storage/sync_types.h"
#include "types.h"

enum class EventType {
  GPS_STATE,
  GPS_DATA,
  BUTTON_PRESS,
  SYNC_COMPLETE,
  // Absolute recording set-on/set-off from the watch over ESP-NOW (issue
  // #138). Serialised through the same queue as screen-originated
  // BUTTON_PRESS commands so AppController keeps state-machine ownership.
  SET_RECORDING,
  // Timestamped event from the watch over ESP-NOW (issue #139): mode
  // changes today, POI marks later. Enters via EspnowTelemetry's receive
  // callback and is serialised through AppController, which owns the
  // dedupe/ack-frontier state (EventIngest).
  MODE_EVENT,
};

struct AppEvent {
  EventType type;
  union EventData {
    GPSData gpsData;
    GPSState gpsState;
    ButtonId buttonId;
    SyncResult syncResult;
    bool recordingDesired;  // SET_RECORDING: true = on, false = off
    // MODE_EVENT: the wire packet itself, carried whole from the receive
    // callback to EventIngest. `version`/`type` were checked by the transport
    // and are along for the ride; copying the struct rather than unpacking it
    // is what keeps a new wire field (oldest_unacked_seq was one, issue #177)
    // from widening every signature between the radio and the sidecar.
    moana::Event modeEvent;
    EventData() { memset(this, 0, sizeof(EventData)); }
  } data;
};
