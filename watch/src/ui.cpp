// UI composition root (issue #138): one LVGL timer is the scheduler. Each
// tick it runs the sleep policy, builds an immutable AppModel snapshot,
// ticks the command channel (radio I/O stays in espnow_link), and hands the
// snapshot to the header + the active tile. No FreeRTOS queues or tasks —
// the LVGL loop is the scheduler on the watch.
#include "ui.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>

#include "alerts.h"
#include "app_model.h"
#include "buzzer.h"
#include "command_channel.h"
#include "control_screen.h"
#include "espnow_link.h"
#include "event_queue.h"
#include "event_store_nvs.h"
#include "header.h"
#include "inputs.h"
#include "mode_screen.h"
#include "rtc_clock.h"
#include "sleep_policy.h"
#include "speed_screen.h"
#include "stats_engine.h"
#include "stats_screen.h"
#include "timebase.h"

namespace ui {

constexpr uint32_t kRefreshMs = 250;

// How long the NO REPLY error state stays up after a command timeout before
// the UI returns to the echoed truth.
constexpr uint32_t kTimeoutBannerMs = 2500;

// Dark for this long and the tile you walked away from is no longer the one
// you want to walk back to: the carousel returns to Speed (issue #159). Timed
// from the screen going off, not from the wake, so the homing happens while
// the panel is still dark — a wake then opens on Speed instead of flicking to
// it from wherever you left off. With nothing recording, home is Control
// instead, so the next glance lands on Start (issue #184).
constexpr uint32_t kSleepHomeMs = 30000;

static lv_obj_t* tileview;
static lv_obj_t* speedTile;
static lv_obj_t* modeTile;
static lv_obj_t* controlTile;
static lv_obj_t* statsTile;

// Carousel columns. The strip is Speed | Mode | Control | Stats and it closes
// into a ring (see onEdgeGesture): from Speed a left swipe reaches Mode and a
// right swipe reaches Stats, so Control — the screen you visit least often —
// sits at the far side of the loop from both. Speed stays column 0, which is
// what the tileview shows on boot.
enum : uint8_t {
  kColSpeed = 0,
  kColMode = 1,
  kColControl = 2,
  kColStats = 3,
};

// Set once per sleep, when the carousel has been sent home; cleared on the
// first awake tick so the next sleep homes again.
static bool homedWhileAsleep = false;

static command_channel::Channel channel;
static bool timeoutBanner = false;
static uint32_t timeoutBannerSinceMs = 0;

// Store-and-forward event queue (issue #139): pure logic + NVS persistence.
static NvsEventStore eventStore;
static event_queue::Queue* eventQueue = nullptr;

// Session/leg statistics (issue #142): pure engine, fed one Input per tick.
static stats_engine::Engine statsEngine;

// Fault alerts (issue #162): pure state machine, one cue per tick at most.
static alerts::Monitor alertMonitor;

// The mode a single press toggles FROM. Display truth is always the boat's
// echo (AppModel.modeEcho); this local base only exists so consecutive
// presses toggle sensibly while out of range, before any echo can confirm.
// It follows the echo whenever the queue is idle (nothing pending that the
// boat hasn't applied yet) and jumps to each press as it is queued.
static uint8_t localModeBase = moana::MODE_UNKNOWN;

// Track packet arrivals so the timebase is fed once per packet, not per tick.
static uint32_t lastFedPacketCount = 0;

// Queue a mode event: press-time UTC from the interim timebase (0 = time
// unknown per protocol), distinct haptic, and screen wake — presses must
// register eyes-free and light the screen (sleep_policy handles the wake on
// the next tick). One wire kind carries every mode (issue #146), so buttons
// and picker taps are the same event, differing only in the mode they name.
static void queueModeEvent(uint8_t mode, uint8_t hapticEffect) {
  sleep_policy::noteActivity();
  inputs::haptic(hapticEffect);
  uint32_t now = millis();
  eventQueue->push(moana::EVENT_MODE_CHANGE, mode, timebase::now(now), now);
  localModeBase = mode;
}

static void onSinglePress() {
  // Toggle SAIL <-> MOTOR; from any other mode (or UNKNOWN) a single press
  // means SAIL — unchanged by the ten-mode picker, the buttons stay the
  // eyes-free quick path.
  if (localModeBase == moana::MODE_SAIL) {
    queueModeEvent(moana::MODE_MOTOR, inputs::kHapticMotor);
  } else {
    queueModeEvent(moana::MODE_SAIL, inputs::kHapticSail);
  }
}

static void onDoublePress() {
  queueModeEvent(moana::MODE_OTHER, inputs::kHapticOther);
}

static void onLongPress() {
  // Reserved for POI marks (issue #140) — detected but deliberately a no-op
  // until that slice lands.
}

// Mode picked on the Mode tile (issue #146): the same queued event a button
// press makes, then back to Speed so the chip confirms on the boat's echo —
// the picker never becomes the display of record.
static void onModePicked(uint8_t mode) {
  if (sleep_policy::touchIsWake(millis())) {
    // The tap that lit the screen, not a choice: LVGL delivers touches while
    // the panel is dark, and setting a mode blind would be worse than making
    // the user tap twice. The touch has already reset the inactivity clock.
    return;
  }
  queueModeEvent(mode, inputs::kHapticModePick);
  lv_tileview_set_tile_by_index(tileview, kColSpeed, 0, LV_ANIM_ON);
}

// Closes the carousel into a ring. LVGL's tileview is a flat strip with no
// wrap, so the two end tiles handle the swipe that has no tile behind it: with
// nothing that way the tileview's scroll_dir excludes that direction, LVGL
// never claims the pan as a scroll, and it arrives here as LV_EVENT_GESTURE
// instead (the same reason the screens' own vertical gestures survive). We turn
// it into a jump to the far end.
//
// LV_ANIM_OFF deliberately: this is a wrap, not a traversal — animating would
// slide through the two tiles in between, which is exactly what the swipe was
// meant to skip.
struct EdgeWrap {
  lv_dir_t gesture;   // finger direction that has no tile behind it
  uint8_t targetCol;  // where the ring continues
};
static const EdgeWrap kSpeedWrap = {LV_DIR_RIGHT, kColStats};
static const EdgeWrap kStatsWrap = {LV_DIR_LEFT, kColSpeed};

static void onEdgeGesture(lv_event_t* e) {
  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) {
    return;
  }
  const EdgeWrap* wrap =
      static_cast<const EdgeWrap*>(lv_event_get_user_data(e));
  if (lv_indev_get_gesture_dir(indev) != wrap->gesture) {
    return;  // vertical gestures belong to the screen on the tile
  }
  lv_tileview_set_tile_by_index(tileview, wrap->targetCol, 0, LV_ANIM_OFF);
}

// The carousel settled on a new tile. Tiles that carry browse state get to
// reset it here rather than each inventing its own "am I visible" test — the
// tileview is the one place that knows. Deliberately not fired on wake: a
// short sleep is meant to hand back exactly the screen you left (issue #159).
static void onTileChanged(lv_event_t*) {
  if (lv_tileview_get_tile_active(tileview) == statsTile) {
    stats_screen::onShown();
  }
}

// PWR crown click = manual leg cut (issue #142): re-asserts the boat's
// current echoed mode through the EXISTING event queue — the server then
// segments on it like any other mode event, and the watch cuts its local
// leg when the echo comes back (the stats engine sees the ack frontier
// advance with an unchanged mode). No new event kind, no local-only state.
static void onPwrClick() {
  sleep_policy::noteActivity();  // a PWR click always lights the screen

  espnow_link::Stats stats = espnow_link::getStats();
  uint8_t mode = stats.packet_count > 0 ? stats.latest.current_mode
                                        : moana::MODE_UNKNOWN;
  if (!statsEngine.sessionLive() || mode == moana::MODE_UNKNOWN) {
    // Not recording (echo-driven, like everything else) or the mode was
    // never classified — there is nothing meaningful to re-assert.
    instance.vibrator();  // error buzz, no event
    return;
  }
  queueModeEvent(mode, inputs::kHapticLegCut);
}

// Hold on the Stats screen's session line: mid-session stats reset,
// watch-local only (the boat's track and accumulator keep running).
static void onStatsResetConfirmed() {
  if (statsEngine.resetSession()) {
    inputs::haptic(inputs::kHapticLegCut);
  } else {
    instance.vibrator();  // nothing live to reset
  }
}

// Latest telemetry echo, shared by the hold callback and the tick.
static void echoSnapshot(uint8_t* echo, bool* echoValid) {
  espnow_link::Stats stats = espnow_link::getStats();
  *echoValid = stats.packet_count > 0;
  *echo = stats.latest.recording_state;
}

static void onHoldConfirmed(bool desiredOn) {
  uint8_t echo;
  bool echoValid;
  echoSnapshot(&echo, &echoValid);
  channel.request(desiredOn, echo, echoValid, millis());
}

static void refreshTimerCb(lv_timer_t*) {
  uint32_t now = millis();
  espnow_link::Stats stats = espnow_link::getStats();
  bool haveTelemetry = stats.packet_count > 0;
  uint32_t ageMs = haveTelemetry ? now - stats.last_rx_ms : UINT32_MAX;
  model::LinkState link = model::classifyLink(haveTelemetry, ageMs);

  // Feed the interim timebase once per fresh packet (#144 swaps the source
  // here). feed() ignores utc_seconds == 0 itself.
  bool freshPacket = haveTelemetry && stats.packet_count != lastFedPacketCount;
  if (freshPacket) {
    lastFedPacketCount = stats.packet_count;
    timebase::feed(stats.latest.utc_seconds, stats.last_rx_ms);
  }

  // Stats engine (issue #142): one Input per tick, packet fields only when
  // fresh. Runs BEFORE the sleep gate — stats must keep accruing (and legs
  // keep cutting) while the screen is dark.
  {
    stats_engine::Engine::Input si;
    si.nowMs = now;
    si.packet = freshPacket;
    si.sogKnots = stats.latest.sog_knots;
    si.fixValid = stats.latest.fix_valid != 0;
    si.hdop = stats.latest.hdop;
    si.cumulativeDistanceM = stats.latest.cumulative_distance_m;
    si.recordingState = stats.latest.recording_state;
    si.currentMode = stats.latest.current_mode;
    si.eventSeqAcked = stats.latest.last_event_seq_acked;
    statsEngine.tick(si);
  }

  // Event queue: trim on the boat's cumulative ack, resend all unacked at
  // 1/s (and immediately after a press). The boat dedupes by seq, so blind
  // resends while out of range are harmless. Runs BEFORE the sleep gate:
  // walking back into range must deliver queued events even if the screen
  // has been dark for an hour.
  if (eventQueue->tick(now, stats.latest.last_event_seq_acked, haveTelemetry)) {
    // Every packet in the burst carries the same head-of-queue seq: it is a
    // statement about the queue, not about the event beside it, and reading
    // it off once means a trim mid-burst can't make two packets disagree.
    const uint16_t oldestUnacked = eventQueue->oldestUnackedSeq();
    for (size_t i = 0; i < eventQueue->pendingCount(); i++) {
      const event_queue::StoredEvent& e = eventQueue->pendingAt(i);
      espnow_link::sendEvent(event_queue::wireEvent(e, oldestUnacked));
    }
  }

  // With nothing pending, the boat's echo is the settled truth — adopt it as
  // the base the next single press toggles from. While events are pending
  // the local base stays on the last press so rapid toggling works offline.
  if (eventQueue->pendingCount() == 0 && haveTelemetry &&
      stats.latest.current_mode != moana::MODE_UNKNOWN) {
    localModeBase = stats.latest.current_mode;
  }

  // Fault alerts (issue #162). Also BEFORE the sleep gate, and for the
  // strongest reason of any of these: the whole point is to reach a wrist
  // that isn't watching the screen. Deliberately buzzes without waking the
  // panel — the haptic is the message, and the header tells the rest of the
  // story whenever you do look.
  {
    alerts::Input ai;
    ai.nowMs = now;
    ai.armed = haveTelemetry &&
               stats.latest.recording_state == moana::RECORDING_ACTIVE;
    ai.linkLost = link == model::LinkState::LOST;
    ai.fixValid = stats.latest.fix_valid != 0;
    switch (alertMonitor.tick(ai)) {
      case alerts::Cue::LINK_LOST:
        inputs::haptic(inputs::kAlertLinkLost);
        buzzer::play(buzzer::Pattern::LINK_LOST);
        break;
      case alerts::Cue::GPS_LOST:
        inputs::haptic(inputs::kAlertGpsLost);
        buzzer::play(buzzer::Pattern::GPS_LOST);
        break;
      case alerts::Cue::CLEARED:
        // Felt, not heard: a fault has to interrupt you, but good news
        // arriving on its own doesn't need to be audible to everyone
        // aboard. Halves the noise without any UI to configure.
        inputs::haptic(inputs::kAlertCleared);
        break;
      case alerts::Cue::NONE:
        break;
    }
    buzzer::tick();  // drops the speaker rail once the tones have finished
  }

  if (sleep_policy::tick()) {
    if (!homedWhileAsleep &&
        sleep_policy::asleepForMs(now) >= kSleepHomeMs) {
      homedWhileAsleep = true;
      bool recording = stats.latest.recording_state == moana::RECORDING_ACTIVE;
      lv_tileview_set_tile_by_index(
          tileview, recording ? kColSpeed : kColControl, 0, LV_ANIM_OFF);
    }
    return;  // no label updates -> no invalidation -> no redraw work
  }
  homedWhileAsleep = false;

  // Command channel: resend while pending, detect the 10 s timeout.
  // Confirmation is content-based on the latest echo; if the link drops
  // mid-pending no new echo arrives, so the channel resends blind until it
  // times out (walking out of range gives up after 10 s, not never).
  uint8_t echo = stats.latest.recording_state;
  bool echoValid = haveTelemetry;
  command_channel::Channel::Tick t = channel.tick(now, echo, echoValid);
  if (t.send) {
    espnow_link::sendCommand(t.desiredOn);
  }
  if (channel.consumeTimeout()) {
    timeoutBanner = true;
    timeoutBannerSinceMs = now;
    instance.vibrator();  // error haptic — command went unacknowledged
  }
  if (timeoutBanner && now - timeoutBannerSinceMs >= kTimeoutBannerMs) {
    timeoutBanner = false;
  }

  // One immutable snapshot per tick; screens are dumb renderers of it.
  model::AppModel m;
  m.telemetry = stats.latest;
  m.haveTelemetry = haveTelemetry;
  m.linkAgeMs = ageMs;
  m.link = link;
  m.recordingEcho = echo;
  m.modeEcho = haveTelemetry ? stats.latest.current_mode : moana::MODE_UNKNOWN;
  // What the watch last asked for, and whether the boat still owes an ack for
  // it — localModeBase already tracks exactly that (it jumps to each press and
  // falls back to the echo once the queue drains).
  m.modeRequested = localModeBase;
  m.modeRequestPending = eventQueue->pendingCount() > 0;
  m.commandPending = channel.pending();
  m.commandDesiredOn = channel.desiredOn();
  m.commandTimedOut = timeoutBanner;
  m.queuePending = (int)eventQueue->pendingCount();
  // A rejected frame is proof the boat is transmitting, so it only means
  // "wrong protocol" while nothing valid is getting through (issue #173).
  // Whether the rejection is recent enough to still be evidence is the Stats'
  // own question; whether anything valid is getting through is this tick's.
  m.protocolMismatch =
      stats.protocolMismatchFresh(now) && link == model::LinkState::LOST;
  m.mismatchVersion = stats.last_mismatch_version;
  m.stats = &statsEngine;
  m.utcSeconds = timebase::now(now);
  m.batteryPercent = instance.pmu.getBatteryPercent();
  m.charging = instance.pmu.isCharging();
  m.nowMs = now;

  header::refresh(m);

  lv_obj_t* active = lv_tileview_get_tile_active(tileview);
  if (active == controlTile) {
    control_screen::refresh(m);
  } else if (active == statsTile) {
    stats_screen::refresh(m);
  } else if (active == modeTile) {
    mode_screen::refresh(m);
  } else {
    speed_screen::refresh(m);
  }
}

void begin() {
  // Timebase first of all (issue #144): the RTC is read once, here, and a
  // plausible time in it means the clock is right before any packet arrives
  // and a mode press in the meantime carries a real UTC rather than the 0 the
  // server discards.
  static rtc_clock::RtcClock rtcClock;
  timebase::begin(&rtcClock, millis());

  // Event queue next: restores unacked events + next seq from NVS, so
  // presses queue (and survivors resend) from the very first tick.
  eventStore.begin();
  static event_queue::Queue queue(eventStore);
  eventQueue = &queue;

  inputs::begin({onSinglePress, onDoublePress, onLongPress, onPwrClick});

  // Wake-touch gate (issue #159). Safe here: main.cpp has already run
  // beginLvglHelper(), so the touch input device exists to wrap.
  sleep_policy::begin();

  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Horizontal carousel, wrapping into a ring — see the kCol* enum and
  // onEdgeGesture. Nav slots in when #141 lands: give it a column and move the
  // Stats end-wrap onto whichever tile ends up last. Tiles are plain black
  // containers; the header floats above them on lv_layer_top().
  tileview = lv_tileview_create(root);
  lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(tileview, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

  speedTile = lv_tileview_add_tile(tileview, kColSpeed, 0, LV_DIR_RIGHT);
  modeTile = lv_tileview_add_tile(tileview, kColMode, 0, LV_DIR_HOR);
  controlTile = lv_tileview_add_tile(tileview, kColControl, 0, LV_DIR_HOR);
  statsTile = lv_tileview_add_tile(tileview, kColStats, 0, LV_DIR_LEFT);

  speed_screen::create(speedTile);
  mode_screen::create(modeTile, onModePicked);
  control_screen::create(controlTile, onHoldConfirmed);
  stats_screen::create(statsTile, onStatsResetConfirmed);
  header::create();

  // Ring wrap on the two end tiles. Both already dropped GESTURE_BUBBLE in
  // their create() (they own the vertical gestures), so they are the gesture
  // target; these callbacks run alongside the screens' own and each ignores
  // the other's axis.
  lv_obj_add_event_cb(speedTile, onEdgeGesture, LV_EVENT_GESTURE,
                      (void*)&kSpeedWrap);
  lv_obj_add_event_cb(statsTile, onEdgeGesture, LV_EVENT_GESTURE,
                      (void*)&kStatsWrap);

  // Fires for every way a tile becomes active — swipe, ring wrap, and the
  // sleep homing — but never for a wake, which changes no tile.
  lv_obj_add_event_cb(tileview, onTileChanged, LV_EVENT_VALUE_CHANGED,
                      nullptr);

  lv_timer_create(refreshTimerCb, kRefreshMs, NULL);
}

}  // namespace ui
