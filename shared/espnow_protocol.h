// ESP-NOW protocol shared between the CoreS3 boat unit (device/) and the
// T-Watch Ultra companion (watch/). Both firmwares must agree on this file;
// bump kProtocolVersion on any struct change.
#pragma once

#include <stdint.h>

namespace moana {

// ESP-NOW only works between peers on the same WiFi channel. The CoreS3 also
// joins infrastructure WiFi, whose AP dictates the channel — if telemetry
// stops after WiFi connects, the AP has moved the radio off this channel.
constexpr uint8_t kEspNowChannel = 1;

constexpr uint8_t kBroadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr uint8_t kProtocolVersion = 7;

enum PacketType : uint8_t {
  PACKET_TELEMETRY = 1,  // boat unit -> watch, 1Hz broadcast
  PACKET_EVENT = 2,      // watch -> boat unit (mode changes, annotations)
  PACKET_COMMAND = 3,    // watch -> boat unit (absolute recording set-on/off)
};

// Wire encoding of the boat unit's RecordingState, echoed in every telemetry
// packet so the watch always displays the boat's true state (issue #138).
enum RecordingStatus : uint8_t {
  RECORDING_IDLE = 0,
  RECORDING_STARTING = 1,
  RECORDING_ACTIVE = 2,
  RECORDING_STOPPING = 3,
  RECORDING_FAILED = 4,
};

// Wire encoding of the boat unit's sailing mode: carried by mode-change
// events and echoed in every telemetry packet (issues #139/#146). The boat
// derives the echo from the latest applied mode event; UNKNOWN until the
// first event after boot (RAM only, not persisted).
//
// One value per server Mode.MODE_CHOICES entry, in the same order — the watch
// picker offers all of them and the sidecar writes their names, so this enum
// and MODE_CHOICES are one vocabulary. See sailingModeName().
enum SailingMode : uint8_t {
  MODE_UNKNOWN = 0,
  MODE_SAIL = 1,
  MODE_MOTORSAIL = 2,
  MODE_MOTOR = 3,
  MODE_ROW = 4,
  MODE_UNPOWERED = 5,
  MODE_ANCHOR = 6,
  MODE_ALONGSIDE = 7,
  MODE_BEACHED = 8,
  MODE_TRAILERED = 9,
  MODE_OTHER = 10,
};

#pragma pack(push, 1)

// 62 bytes — well inside ESP-NOW's 250-byte frame limit.
//
// cumulative_distance_m (issue #142, scoping decision): distance is
// integrated on the BOAT, not the watch — the boat always has GPS, so
// nothing is lost while the watch is out of range, asleep, or rebooting
// (the echo restores session state instantly). Metres accumulated between
// consecutive valid fixes while recording is ACTIVE; 0 at recording start;
// holds its last value after stop. The watch derives leg/session distance
// purely by snapshot diffs of this echo — no watch-side integration.
struct Telemetry {
  uint8_t version;
  uint8_t type;  // PACKET_TELEMETRY
  uint16_t seq;
  double lat;
  double lng;
  float sog_knots;
  float cog_deg;
  float heel_deg;
  float trim_deg;
  float heading_deg;
  float altitude_m;
  float hdop;
  uint8_t satellites;
  uint8_t fix_valid;        // 0 = ignore position fields
  uint8_t recording_state;  // RecordingStatus
  uint32_t utc_seconds;     // Unix epoch from GPS time, 0 = unknown
  uint8_t current_mode;     // SailingMode — latest applied mode event echo
  // Cumulative (contiguous) ack frontier for watch->boat Event packets:
  // the highest seq S such that every event 1..S has been received.
  // kEventSeqNone = no events ever received. See the eventSeq* helpers.
  // The watch's half of the same conversation is Event::oldest_unacked_seq.
  uint16_t last_event_seq_acked;
  // Metres recorded so far this session (see struct comment above).
  uint32_t cumulative_distance_m;
};

// One kind per *category* of watch event, not per mode (issue #146): ten
// modes would need ten kinds, so the mode travels in the Event's mode field.
enum EventKind : uint8_t {
  EVENT_MODE_CHANGE = 1,  // `mode` carries the SailingMode to apply
  EVENT_VOICE_MARKER = 4, // audio recorded on watch, blob syncs separately
};

// Watch -> boat timestamped event (issue #139). Reliable delivery is
// store-and-forward: the watch queues events in flash, resends all unacked
// events 1/s, and trims on the telemetry ack frontier; the boat dedupes by
// seq and only advances the frontier contiguously.
struct Event {
  uint8_t version;
  uint8_t type;  // PACKET_EVENT
  uint16_t seq;         // never kEventSeqNone; assigned by the watch queue
  uint8_t kind;         // EventKind
  uint8_t mode;         // SailingMode for EVENT_MODE_CHANGE, else MODE_UNKNOWN
  uint32_t utc_seconds; // 0 if the watch had no time reference
  // The lowest seq the watch still holds unacked — the other half of the ack
  // conversation, and the reason the boat never has to *guess* what a gap
  // means (issue #177). A cumulative frontier can say "I have everything up
  // to S" but not "I am missing exactly this one", so before this field the
  // boat inferred an unfillable gap from a repeated offer, which was wrong
  // whenever the same packet was simply lost twice running.
  //
  // Every packet in a resend burst carries the same value: the head of the
  // watch's queue. Seqs *below* it will never be offered again — they were
  // either acked already or dropped on queue overflow — so a boat waiting on
  // one of them is waiting forever and may step over it. kEventSeqNone means
  // the watch declined to say (never sent by this firmware).
  uint16_t oldest_unacked_seq;
};

// Watch -> boat recording control. Absolute set-on/set-off semantics, never
// toggle: the watch resends 1/s until the telemetry echo confirms, and a
// re-delivered SET to the state the boat is already in must be a no-op —
// a toggle would double-fire under retries.
struct Command {
  uint8_t version;
  uint8_t type;  // PACKET_COMMAND
  uint16_t seq;
  uint8_t desired_recording;  // 0 = stop recording, 1 = start recording
};

#pragma pack(pop)

// Both firmwares compile against these layouts and nothing in either build
// catches a one-sided change, so the sizes are pinned here: a reordered field,
// a widened type or a toolchain that ignores the pack pragma fails to compile
// on both ends instead of corrupting data on one. A deliberate layout change
// updates the number here *and* kProtocolVersion.
static_assert(sizeof(Telemetry) == 62,
              "Telemetry layout changed - update this size and bump kProtocolVersion");
static_assert(sizeof(Event) == 12,
              "Event layout changed - update this size and bump kProtocolVersion");
static_assert(sizeof(Command) == 5,
              "Command layout changed - update this size and bump kProtocolVersion");

// ESP-NOW carries at most 250 bytes of payload per frame. Telemetry is the
// largest packet, leaving 188 bytes of headroom for future fields.
static_assert(sizeof(Telemetry) <= 250, "Telemetry exceeds the ESP-NOW frame limit");

// Both receive callbacks dispatch on frame length before they read `type`
// (device/src/connectivity/espnow_telemetry.cpp, watch/src/espnow_link.cpp),
// so two watch->boat packets of equal size would be indistinguishable on the
// wire. Adding a third watch->boat struct means giving it a distinct size or
// changing both dispatchers to key on `type` first.
static_assert(sizeof(Command) != sizeof(Event),
              "watch->boat packets must differ in size; the receive callbacks dispatch on length");

// ============================================================
// The version gate (issue #173)
// ============================================================

enum class FrameVersion : uint8_t {
  OK,        // speaks kProtocolVersion — go on and dispatch on length
  TOO_SHORT, // not even a version byte; a damaged frame
  MISMATCH,  // a real packet from firmware that isn't this one
};

// The first thing both receive callbacks do, before anything reads a length or
// a type. `version` is byte 0 of every packet in every version of this
// protocol — the one field at a fixed offset — so it stays readable even when
// the layout change that caused the mismatch is what moved the frame length
// the callbacks dispatch on: Event went 10 to 12 bytes at v7. Check the
// version *first*, or a mismatched frame is thrown away as a stray of the
// wrong length and the half-finished reflash behind it stays invisible.
//
// Lives here rather than twice in two firmwares because it is a rule about the
// wire, and a rule about the wire that each end implements for itself is a rule
// only until one end is edited. `*version` carries the byte the frame claimed,
// for the mismatch counters both ends keep.
inline FrameVersion checkFrameVersion(const uint8_t* data, int len,
                                      uint8_t* version) {
  *version = 0;
  if (data == nullptr || len < 1) {
    return FrameVersion::TOO_SHORT;
  }
  *version = data[0];
  return *version == kProtocolVersion ? FrameVersion::OK
                                      : FrameVersion::MISMATCH;
}

// ============================================================
// Contiguous-ack window arithmetic (issue #139)
// ============================================================
// Shared by both firmwares (and both native test suites) so there is exactly
// one implementation of the uint16 wraparound rules. Event seqs live on a
// mod-2^16 circle; kEventSeqNone (0) is reserved as the "no events ever
// received" ack frontier, so the watch never assigns seq 0 (nextEventSeq
// skips it on wrap).

constexpr uint16_t kEventSeqNone = 0;

// The seq the watch assigns after `seq` — increments, skipping the reserved
// kEventSeqNone on wrap. nextEventSeq(kEventSeqNone) == 1 is also the very
// first seq ever assigned.
inline uint16_t nextEventSeq(uint16_t seq) {
  uint16_t next = (uint16_t)(seq + 1);
  return next == kEventSeqNone ? (uint16_t)1 : next;
}

// True when a cumulative ack at `frontier` covers `seq` — i.e. seq is at or
// behind the frontier on the wrapping circle (serial-number arithmetic, half
// window of 2^15). False for the kEventSeqNone frontier: nothing is acked.
// Valid while fewer than 2^15 events are in flight, which the watch's
// bounded queue guarantees by construction.
inline bool eventSeqAcked(uint16_t seq, uint16_t frontier) {
  if (frontier == kEventSeqNone || seq == kEventSeqNone) {
    return false;
  }
  return (uint16_t)(frontier - seq) < 0x8000;
}

// True when `seq` is exactly the next event expected after `frontier` — the
// only arrival allowed to advance a contiguous ack.
inline bool eventSeqIsNext(uint16_t seq, uint16_t frontier) {
  return seq == nextEventSeq(frontier);
}

// The seq before `seq`, skipping the reserved kEventSeqNone on wrap — the
// exact inverse of nextEventSeq, so nextEventSeq(prevEventSeq(s)) == s for
// every seq the watch can assign. Used to set a frontier that covers
// everything *below* a known-oldest-unacked seq.
//
// Note prevEventSeq(1) is 0xFFFF, not kEventSeqNone: seq 1 follows the wrap
// as well as opening time, and the one caller never asks about a seq that is
// already the next expected one (which is the only case where "nothing acked
// yet" would be the answer).
inline uint16_t prevEventSeq(uint16_t seq) {
  uint16_t prev = (uint16_t)(seq - 1);
  return prev == kEventSeqNone ? (uint16_t)0xFFFF : prev;
}

// True when the watch's `oldestUnacked` proves the gap ahead of `frontier`
// can never be filled (issue #177): the watch no longer holds the seq the
// boat is waiting for, so no resend will ever offer it and a contiguous
// frontier would freeze here for the rest of the session.
//
// False in the two ordinary cases — the watch's oldest unacked IS what we are
// waiting for, or it is something we already acked — and false for a value
// ahead of the packet carrying it, which no well-behaved watch sends and
// which would otherwise let one malformed frame skip real events.
inline bool eventSeqGapIsPermanent(uint16_t oldestUnacked, uint16_t frontier,
                                   uint16_t carriedBySeq) {
  if (oldestUnacked == kEventSeqNone) {
    return false;  // the watch declined to say
  }
  if (!eventSeqAcked(oldestUnacked, carriedBySeq)) {
    return false;  // ahead of its own packet — not credible
  }
  return !eventSeqAcked(oldestUnacked, frontier) &&
         !eventSeqIsNext(oldestUnacked, frontier);
}

// ============================================================
// Sailing-mode vocabulary (issue #146)
// ============================================================

// True for the modes a watch may actually select. MODE_UNKNOWN is an echo
// state ("no mode applied since boot"), never a selection, and anything above
// MODE_OTHER is a value only a newer firmware knows.
inline bool isSelectableSailingMode(uint8_t mode) {
  return mode >= MODE_SAIL && mode <= MODE_OTHER;
}

// The mode a received event selects; MODE_UNKNOWN for kinds that are not mode
// changes (e.g. EVENT_VOICE_MARKER) and for out-of-range mode values.
inline uint8_t sailingModeForEvent(uint8_t kind, uint8_t mode) {
  if (kind != EVENT_MODE_CHANGE || !isSelectableSailingMode(mode)) {
    return MODE_UNKNOWN;
  }
  return mode;
}

// The server's Mode.MODE_CHOICES string for a wire mode — the single mapping
// shared by the device's sidecar writer and the watch's UI labels, so the
// vocabulary can't drift between firmwares. Keep in step with
// server/moana/tracks/models.py (MODE_CHOICES) and events_jsonl.py
// (MODE_KINDS), which parses these names back.
inline const char* sailingModeName(uint8_t mode) {
  switch (mode) {
    case MODE_SAIL:
      return "SAIL";
    case MODE_MOTORSAIL:
      return "MOTORSAIL";
    case MODE_MOTOR:
      return "MOTOR";
    case MODE_ROW:
      return "ROW";
    case MODE_UNPOWERED:
      return "UNPOWERED";
    case MODE_ANCHOR:
      return "ANCHOR";
    case MODE_ALONGSIDE:
      return "ALONGSIDE";
    case MODE_BEACHED:
      return "BEACHED";
    case MODE_TRAILERED:
      return "TRAILERED";
    case MODE_OTHER:
      return "OTHER";
    default:
      return "UNKNOWN";
  }
}

// Converts TinyGPSPlus raw date/time (DDMMYY, HHMMSSCC) to a Unix epoch.
// Returns 0 if the date is missing — callers treat 0 as "time unknown".
// Days-from-civil per Howard Hinnant's algorithm, valid for all GPS dates.
inline uint32_t utcEpochFromGps(uint32_t ddmmyy, uint32_t hhmmsscc) {
  if (ddmmyy == 0) {
    return 0;
  }
  const int d = (int)(ddmmyy / 10000);
  const int m = (int)((ddmmyy / 100) % 100);
  int y = 2000 + (int)(ddmmyy % 100);
  y -= m <= 2;
  const int era = y / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + doe - 719468;
  const uint32_t hh = hhmmsscc / 1000000;
  const uint32_t mm = (hhmmsscc / 10000) % 100;
  const uint32_t ss = (hhmmsscc / 100) % 100;
  return (uint32_t)(days * 86400L) + hh * 3600 + mm * 60 + ss;
}

}  // namespace moana
