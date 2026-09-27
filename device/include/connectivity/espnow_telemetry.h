#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "espnow_protocol.h"

// ESP-NOW link to the T-Watch companion (issues #133/#138): broadcasts
// moana::Telemetry at 1Hz and receives moana::Command packets.
//
// Pure transport: App composes the Telemetry payload (it owns recording/GPS/
// IMU state); this class stamps version/type/seq and transmits. Received
// commands and events are validated here and handed to onCommand/onEvent,
// which App wires to postEvent() so they are serialised through
// AppController exactly like screen-originated commands.
//
// Owns the radio when infrastructure WiFi is idle: begin() brings the radio
// up in STA mode on moana::kEspNowChannel. When Connectivity joins an AP the
// channel follows the AP (the watch misses packets on other channels — known
// dockside limitation), and when Connectivity turns the radio off it must
// call restart() to hand it back. restart() re-arms both the broadcast peer
// and the receive callback.
class EspnowTelemetry {
 public:
  // Returns the payload to broadcast; version/type/seq are stamped here.
  // Called on the send task — must be safe to call from any task.
  using TelemetrySnapshotFn = std::function<moana::Telemetry()>;
  // Invoked (on the WiFi task) for each valid Command packet.
  using CommandFn = std::function<void(bool desiredRecording)>;
  // Invoked (on the WiFi task) for each valid Event packet (issue #139). The
  // whole packet, not its fields one by one: it travels unchanged from here
  // through the app queue into EventIngest, and a field added to the wire
  // should widen the struct it belongs to rather than every signature on the
  // way (oldest_unacked_seq, issue #177, is what taught us that).
  using EventFn = std::function<void(const moana::Event& event)>;

  // Frames this link rejected, and why (issue #173). A rejection is the right
  // call — it is what stops a layout disagreement from misparsing into
  // plausible-looking data — but it is also silent, and a silent rejection is
  // indistinguishable from a watch that is switched off. These counters are
  // the difference, and they are per reason because the reasons mean very
  // different things: a version mismatch is a half-finished reflash on the
  // bench, a foreign type on the right version is somebody else's ESP-NOW
  // traffic on channel 1.
  struct RxStats {
    uint32_t versionMismatches;   // version byte isn't kProtocolVersion
    uint8_t lastMismatchVersion;  // what that frame claimed to be
    uint32_t foreignTypes;        // our version and size, not our packet type
    uint32_t unknownLengths;      // our version, no struct that size
  };

  void begin(TelemetrySnapshotFn compose, CommandFn onCommand,
             EventFn onEvent);

  // Re-arms ESP-NOW after something else (Connectivity) reset the radio.
  void restart();

  // Safe from any task; the counters are atomics written by the WiFi task.
  RxStats rxStats() const;

 private:
  bool initRadio();
  void noteVersionMismatch(uint8_t version);
  // A frame on our own version that we have no use for: `foreignType` true for
  // a packet type we don't handle, false for a length no struct has.
  void noteStrayFrame(bool foreignType);
  void sendOnce();
  static void sendTask(void* pvParameters);
  // esp_now_register_recv_cb takes a bare function pointer, no user arg —
  // trampoline through a static instance pointer.
  static void onReceiveStatic(const uint8_t* mac, const uint8_t* data, int len);

  TelemetrySnapshotFn compose;
  CommandFn onCommand;
  EventFn onEvent;
  uint16_t seq = 0;
  std::atomic<bool> radioReady{false};

  std::atomic<uint32_t> versionMismatches{0};
  std::atomic<uint8_t> lastMismatchVersion{0};
  std::atomic<uint32_t> foreignTypes{0};
  std::atomic<uint32_t> unknownLengths{0};
  // Rate limits for the two rejection logs: the watch resends 1/s and
  // telemetry never stops, so an unbounded log would fill with one fault.
  // Separate timers on purpose — a noisy channel must not suppress the
  // reflash warning, which is the one a person has to act on.
  std::atomic<unsigned long> lastMismatchLogMs{0};
  std::atomic<unsigned long> lastStrayLogMs{0};

  static EspnowTelemetry* instance_;

  static constexpr unsigned long SEND_INTERVAL_MS = 1000;
  static constexpr unsigned long MISMATCH_LOG_INTERVAL_MS = 10000;
  static constexpr unsigned long STRAY_LOG_INTERVAL_MS = 60000;
};
