# Two traces: a mode tap, and a recording start

The root `CLAUDE.md` "Data flow" section names the hops. This document walks two of them
function by function, saying what the payload looks like as it leaves each one, and then
answers the failure questions that only exist *between* the hops — where each module is
individually correct and the contract between two of them is what slipped (issue #153).

Written against `67efc91`. Line numbers drift; the function names are the durable part.

---

## Trace 1 — a mode tap becomes a `Mode` row

### 1. The tap → the queue (watch, LVGL loop task)

`mode_screen`'s row callback, or a GPIO0 press, or the PWR-click leg cut, all land on the same
function — one wire kind carries every mode since issue #146:

| Entry point | `watch/src/ui.cpp` |
|---|---|
| Mode tile row tap | `onModePicked(mode)` :125 |
| GPIO0 single press | `onSinglePress()` :102 — toggles SAIL ⇄ MOTOR off `localModeBase` |
| GPIO0 double press | `onDoublePress()` :113 — OTHER |
| PWR crown click | leg cut :193 — re-asserts the *echoed* mode |

All four call `queueModeEvent(mode, haptic)` (`ui.cpp:94`), which wakes the screen, fires the
haptic, and:

```cpp
eventQueue->push(moana::EVENT_MODE_CHANGE, mode, timebase::now(now), now);   // ui.cpp:98
localModeBase = mode;
```

`timebase::now()` (`watch/src/timebase.cpp`) is the last known UTC plus the `millis()` elapsed
since it was observed. That reference is the last telemetry packet's `utc_seconds` — or, before any
packet arrives, whatever the hardware RTC was holding at `timebase::begin()` (issue #144), which is
why a watch rebooted out of range still stamps real times. It is **0 only when neither has ever
supplied one**: a watch whose RTC has never been disciplined, which is the protocol's documented
"time unknown". Nothing downstream of here re-stamps it.

`event_queue::Queue::push` (`watch/src/event_queue.cpp:73`) assigns the next seq from a counter
that is never reused and survives reboot, appends, `persist()`s the whole queue to NVS, and arms
an immediate send.

**Leaving push:** a `StoredEvent{seq, kind=EVENT_MODE_CHANGE, mode, utc_seconds}` in RAM and in
the NVS blob `moana-evt` (magic `"MEV2"`, ~8 B header + 8 B per record, 64 records max).

### 2. The queue → the air (watch, same tick)

Once per 250 ms tick, `refreshTimerCb` (`ui.cpp:220`) calls:

```cpp
if (eventQueue->tick(now, stats.latest.last_event_seq_acked, haveTelemetry)) {  // ui.cpp:257
  const uint16_t oldestUnacked = eventQueue->oldestUnackedSeq();
  for (size_t i = 0; i < eventQueue->pendingCount(); i++) {
    const event_queue::StoredEvent& e = eventQueue->pendingAt(i);
    espnow_link::sendEvent(event_queue::wireEvent(e, oldestUnacked));
  }
}
```

`tick()` first trims every event at or behind the boat's echoed frontier (`moana::eventSeqAcked`),
then returns true immediately after a push and once per second while anything is pending. It is
a **resend-all-unacked burst**, not a per-event retry — a lost packet self-heals within ~1 s and
the boat dedupes by seq. It runs *before* the sleep gate, so a dark screen still delivers.

Every packet in the burst carries the same `oldest_unacked_seq` — the head of the queue, read once
so a trim can't make two packets in one burst disagree. It is a statement about the *queue*, not
about the event beside it: seqs below it are acked or dropped and will never be sent again. That is
the watch's half of the ack conversation (issue #177), and step 4 is where it earns its two bytes.

`event_queue::wireEvent` builds the `moana::Event`; `espnow_link::sendEvent`
(`watch/src/espnow_link.cpp`) stamps `version`/`type` — seq belongs to the queue, unlike the
per-boot command seq — and broadcasts. The packet travels as one struct from here to the sidecar,
which is what stopped `oldest_unacked_seq` from widening five signatures on its way through.

**On the air:** one `moana::Event` (fixed size, `static_assert`ed in `shared/espnow_protocol.h`),
broadcast on `kEspNowChannel`, no retry, no ack at the radio layer.

### 3. The air → AppController (boat, WiFi task → app task)

`EspnowTelemetry::onReceiveStatic` (`device/src/connectivity/espnow_telemetry.cpp:93`) checks
`version` **first**, off byte 0, which is at the same offset in every version of every packet.
Only then does it dispatch on frame length and confirm `type` — which is why an assert in the
shared header holds `sizeof(Command) != sizeof(Event)`. The order matters: a layout bump can move
the length as well as the contents (`Event` went 10 → 12 bytes at v7), so a size check first would
throw a mismatched frame away as a stray rather than counting it as the flash-time mistake it is.
The gate itself is `moana::checkFrameVersion`, in the shared header, so both firmwares run the
same rule. Rejected frames land in `rxStats()` — a version mismatch, a foreign type, an unknown
length — and are logged: a rate-limited `LOG_WARN` naming both versions for a mismatch (the fault a
person has to act on), a quieter, separately rate-limited `LOG_DEBUG` for stray traffic on our own
version (issue #173).

The callback runs on the WiFi task, so it does nothing but hand over:

```cpp
AppEvent event;
event.type = EventType::MODE_EVENT;
event.data.modeEvent = wire;   // the moana::Event itself — device/src/app.cpp
postEvent(event);
```

`App::postEvent` (`app.cpp:185`) is the **non-critical** path: `xQueueSend` with a 100 ms timeout
onto the 50-slot app queue, and a `LOG_WARN` if it still can't fit. Dropping here is safe — the
frontier does not advance, so the watch re-offers the seq a second later.

### 4. Dedupe and the ack frontier (boat, app task)

`AppController::handleModeEvent` (`device/src/events/app_controller.cpp:51`):

```cpp
std::vector<moana::Event> applied = eventIngest.accept(event);
uint16_t settled = eventAckFrontier;   // published only for what the loop settles
```

The frontier is published at the *end* of the apply loop, not here — that ordering is issue #180
and step 5 owns it. On the clean path it is published as `eventIngest.ackFrontier()`, which is the
only value that includes a dead range the ingest jumped over: no applied event's seq reaches it.

`EventIngest::accept` (`device/src/events/event_ingest.cpp`) is TCP-style cumulative ack over
uint16 seqs:

- **first**, the packet's `oldest_unacked_seq`: if it is ahead of the seq the frontier is waiting
  for, everything between is gone for good, so the frontier jumps to `prevEventSeq(head)`.
  `recoverPendingBehindFrontier()` then hands back, in seq order, anything buffered *inside* the
  range just jumped — the watch will not send those again, but the boat has them, and binning them
  would lose a delivered mode change inside the fix for losing mode changes — and
  `drainContiguous()` applies the buffered successors ahead of the frontier too
- at or behind the frontier → duplicate, returns empty
- exactly next → frontier advances, event applied, then `drainContiguous()` applies any buffered
  successors **in seq order**
- past a hole the watch still holds → buffered (16 slots), frontier unmoved

That first rule is the whole of issue #177, and it replaced an inference. The frontier is a single
cumulative number: it can say "I have everything up to S" but never "I am missing exactly this
one", so the boat used to read a *re-offer* of an already-buffered seq as proof that nothing
earlier existed. That was right for a rebooted boat and wrong for a packet lost twice running,
which ESP-NOW's retry-free broadcast makes ordinary — and it could not help at all once the
frontier was live, so a seq the watch dropped on overflow froze delivery until the boat rebooted.
The watch now just says what it holds, and the boat never guesses.

Two guards on believing it, both in `moana::eventSeqGapIsPermanent`: a head ahead of the seq in its
own packet is not credible (no watch sends that; a damaged frame might), and `kEventSeqNone` means
"declined to say". Neither ever moves the frontier backwards, which is what keeps this rule and
#180's `holdFrontierAt` from fighting: after a hold the watch still holds the refused seq, so its
head *is* what the boat is waiting for, and nothing jumps.

For each applied event, `moana::sailingModeForEvent` validates the mode; a known one sets
`currentMode` (an atomic read by the telemetry composer), an unknown one is logged and **acked
anyway** so a newer watch firmware doesn't resend forever.

### 5. The sidecar (boat, app task)

```cpp
switch (cb.recording.appendEvent(e.kind, mode, e.utc_seconds)) {
  case EventPersist::WRITTEN:    break;
  case EventPersist::NO_SESSION: LOG_INFO(...); break;    // deliberate ack
  case EventPersist::FAILED:     eventIngest.holdFrontierAt(settled); return;
}
settled = e.seq;
```

`Recorder::appendEvent` (`device/src/recording/recorder.cpp:100`) snapshots `sidecarPath_` under a
spinlock, answers `NO_SESSION` on an empty path, formats via `EventJsonl::formatLine`, and does an
open-append-flush-close per event:

```
{"kind":"SAIL","utc":1753900000}\n
```

`kind` carries the *mode name* from `moana::sailingModeName` — the sidecar vocabulary is the wire
protocol's, deliberately unchanged when the wire kind collapsed to one `EVENT_MODE_CHANGE`.

The path is published by the recorder task **before** the ACTIVE transition (`recorder.cpp:218`)
and cleared to `""` after `session.end()` (`recorder.cpp:305`). The file itself is created lazily
on the first event, so a track with no mode changes gets no sidecar at all.

### 6. The echo back to the wrist (boat → watch, 1 Hz)

`App::composeTelemetry` (`device/src/app.cpp:313`) puts `current_mode` and
`last_event_seq_acked` into every telemetry packet. On the watch those two land in the same
`espnow_link::Stats` the tick reads: the frontier trims the queue, `current_mode` drives the mode
chip on Speed, the filled row on the Mode tile, and `stats_engine`'s leg cuts. A queued-but-unacked
tap shows as an *outlined* row — the picker is never the display of record.

### 7. Off the boat (SD sync → device HTTP → frontend → server)

- `Storage::listLocalEventSidecars` (`device/src/storage/storage.cpp:411`) appends sidecars after
  their CSVs in the sync list (`:304`), so a sidecar is never archived before its CSV has had its turn.
- The device web server serves both off one endpoint: `GET /track?file=…` (`web_server.cpp:49`),
  guarded by `TrackFilename::isValid || isEventSidecar`.
- `fetchTrackEvents` (`frontend/src/api/device.ts:107`) derives `gps_X.events.jsonl` from the CSV
  name and returns `null` for *any* failure (issue #174).
- `DeviceTracksListView` (`:105`) wraps the text in a `File` and hands both to
  `uploadTrackCsv(file, meta, events)` (`frontend/src/api/web.ts:450`) — **one multipart POST**,
  `file` + `events`, never two requests.

### 8. `Mode` rows (server)

`TrackViewSet.upload_csv` (`server/moana/tracks/viewsets.py:330`):

1. CSV parsed first; an unparseable CSV is a 400 and the sidecar is never looked at.
2. `parse_events_jsonl` (`events_jsonl.py`) — per-line, never raises: bad JSON, unknown kind,
   non-int/bool/negative `utc`, `utc > 0xFFFFFFFF`, and **`utc == 0`** each become a warning and a
   skipped line.
3. `collapse_mode_bursts` — a maximal run of events within `MODE_DEBOUNCE_SECONDS` (20 s) of the
   run's *first* event collapses to **the first event's timestamp carrying the last event's mode**.
   Same duration as `kMinLegMs` in `watch/src/stats_engine.h`; nothing checks the pair.
4. `events_outside_window` — reported, never dropped; ±1 h tolerance around the track's range.
5. No vessel → the whole sidecar is dropped with a warning (`Mode` is vessel-anchored).
6. Inside one `transaction.atomic()`: `Track`, `TrackPoint`s, then
   `Mode.objects.get_or_create(vessel=…, started_at=…, defaults={mode, metadata={"source":"watch"}})`
   (`viewsets.py:457`) — idempotent on the model's `(vessel, started_at)` uniqueness, so a re-upload
   leaves a mode reclassified in the web UI alone.

**On the map:** `Journey.get_modes` / `Leg.get_modes` (`models.py:410`, `:466`) select
`Mode` rows with `started_at` **inside** the journey's own window; `_postgis_mode_stats`
(`statistics.py:91`) ends each mode at the next one's `started_at`. A mode row is not a track's
property — it is a point on the vessel's timeline, and it does not reach backwards.

---

## Trace 2 — hold-to-record becomes a state echo

### 1. The hold (watch)

`control_screen`'s 1 s fill sweep completes → `onHoldConfirmed(desiredOn)` (`ui.cpp:213`) reads
the latest echo and calls `command_channel::Channel::request(desiredOn, echo, echoValid, now)`.

`request` (`watch/src/command_channel.cpp:16`) is idempotent twice over: a request the echo
already confirms is a no-op, and re-requesting the pending state does not reset the timeout
clock. The opposite of an in-flight command replaces it outright.

**State after:** `pending_ = true`, `desiredOn_`, `sentOnce_ = false`, `requestedAtMs_`.

### 2. The resend loop (watch, per tick)

```cpp
command_channel::Channel::Tick t = channel.tick(now, echo, echoValid);   // ui.cpp:321
if (t.send) espnow_link::sendCommand(t.desiredOn);
```

`tick` sends immediately, then once per second; clears `pending_` when the echo confirms — but
only after `sentOnce_`, so a command that replaced its opposite can never be swallowed by an echo
still showing the old state. At 10 s it gives up and latches `timedOut_` for the UI
(`consumeTimeout()`), and the display falls back to the echoed truth.

`espnow_link::sendCommand` stamps a **per-boot** seq (unlike events) — the boat never reads it.

**On the air:** `moana::Command{version, type, seq, desired_recording}` — absolute on/off, never
a toggle.

### 3. The boat applies it

`onReceiveStatic` → `onCommand(cmd.desired_recording != 0)` → `App::postEvent(SET_RECORDING)`
(`app.cpp:110`), same non-critical queue as a mode event. `AppController::process` logs it and
calls `cb.recording.setRecording(desired)` (`app_controller.cpp:39`).

`Recorder::setRecording` (`recorder.cpp:63`) routes through the pure
`RecordingStateMachine::setOnAction` / `setOffAction`, which is where the idempotence lives:

| Current | `setOn` | `setOff` |
|---|---|---|
| IDLE | START | NONE |
| STARTING | NONE | NONE (retry lands at ACTIVE) |
| ACTIVE | NONE | STOP |
| STOPPING | NONE (retry lands at IDLE) | NONE |
| FAILED | **NONE** — a retried "start" must not silently clear a fault | RESET |

There is no dedupe on the command seq and none is needed: absolute + idempotent means a duplicate
packet is a no-op by construction.

START → `transitionState(STARTING)` → `start()` spawns the recorder task, which opens the CSV,
publishes the sidecar path, then transitions ACTIVE. STOP → `transitionState(STOPPING)` → the task
observes `shouldStop` within its 250 ms queue poll, drains, `session.end()`, clears the sidecar
path, transitions IDLE (or FAILED).

### 4. The echo closes the loop

`composeTelemetry` sets `recording_state` from `toWireRecordingState(recording.getState())`. The
watch's `Channel::confirms` treats STARTING **or** ACTIVE as confirming "on" (STARTING already
proves the boat heard it) and STOPPING or IDLE as confirming "off". The Control tile renders the
echo, not the request; `alerts` arms only on a last-known ACTIVE echo, which is why a watch on the
charger beside a switched-off boat stays silent.

---

## The questions, answered

### A 5-minute outage, then the queue drains at once — order and timestamps?

**Order: correct, and it does not depend on the wire.** The queue holds events in seq order and
resends `pendingAt(0..n)` in that order; `EventIngest` only ever applies contiguously, in seq
order, buffering anything past a hole; `appendEvent` is called in that order, so the sidecar's line
order is press order. Even if it weren't, `collapse_mode_bursts` sorts by `started_at` and
`Mode` rows are ordered by `started_at`, so file order is not load-bearing.

**Timestamps: press time, not delivery time — provided a reference was ever seen.** `push` stamps
`timebase::now()` at the tap and nothing re-stamps it later; `timebase` keeps extrapolating from
the last reference through the outage (crystal drift over 5 minutes is milliseconds). A watch rebooted
out of range is covered too, since #144: `timebase::begin()` adopts the RTC's time, and the RTC is
rewritten whenever telemetry shows it has drifted — or when a write was refused, because the drift
check compares telemetry against the RAM estimate that same packet rebases, so an unconfirmed write
is the only thing that can ask for a retry.

What is still stamped 0 is a watch that has *never* had a reference: fresh out of the box, or with
an RTC whose oscillator has stopped since it was last set. Those events are delivered correctly and
dropped at import. That is **issue #179**, which #144 narrowed rather than closed.

Two second-order effects worth knowing:

- The drain is a tight loop of up to 64 `esp_now_send` calls in one tick. ESP-NOW broadcast has no
  retry and the tx path is finite, so the tail of a large burst can fail; `sendEvent` logs and
  returns false, and the next second's burst re-offers everything. Delivery converges rather than
  completing in one pass.
- On the boat, 64 frames arriving in a few milliseconds contend with GPS data on the 50-slot app
  queue. Drops there are safe (no frontier advance), but a lost *middle* seq leaves up to 59
  successors racing for 16 `EventIngest` buffer slots, and the burst re-runs each second until the
  hole fills. Again: converges, but not in one pass.

### Mode set while recording is stopped, then recording starts — which track?

**Neither.** With no session, `sidecarPath_` is `""`, `appendEvent` answers `NO_SESSION`, and the
event is echoed and acked but never written. Nothing replays it when recording starts: the recorder task
publishes the path and goes ACTIVE without consulting `currentMode`.

The server side makes this worse than "the previous mode carries over". `Journey.get_modes` filters
`started_at__gte=journey.started_at`, so a `Mode` row from before the recording is not in the
journey's window at all — the opening stretch of the track has *no* mode, and `_postgis_mode_stats`
has nothing to attribute it to until the first press *inside* the recording.

This agrees with **#145** (write the initial mode as the first sidecar event at recording start),
whose acceptance criteria name exactly this case. #145's "related edge" — an event delivered after
recording stops, whose UTC falls inside the track — is the same code path in the other direction.

### Re-upload, and a CSV upload that fails half way

**Re-upload of the same CSV**: creates a *second* `Track` — `source_filename` is written and never
read anywhere in the server. The sidecar half is idempotent (`get_or_create` on
`(vessel, started_at)`), so re-uploading is safe for the watch's modes and duplicating for the
track. That asymmetry is **issue #176**; the frontend's "Uploaded" pill is presentation, not a
constraint.

**CSV fails half way**: nothing is half-written. The parse happens before any DB work (a bad CSV is
a 400 and the sidecar is never touched), and `Track` + `TrackPoint`s + `Mode` rows are created
inside one `transaction.atomic()`. A connection dropped mid-request rolls the whole thing back, and
the retry is clean. The one asymmetry: the sidecar is parsed and debounced *before* the transaction,
so its warnings are computed against a track that may never exist — harmless, because they are only
ever returned in the same response.

**Sidecar present, CSV absent**: not expressible. There is one endpoint and one multipart request;
`events` without `file` is a 400 before anything else runs.

### Recording stop and a mode set in the same second, either order

Two different races, and they resolve differently.

**On the boat**, `appendEvent` (app task) and `setSidecarPath("")` (recorder task) are serialised by
`sidecarMux_`, so there is no torn path — but the *outcome* depends on which wins:

- Event first → appended to the sidecar of the session that is ending. Its UTC is likely after the
  track's `ended_at` (which is the last GPS *fix*, not the stop), so it lands in
  `events_outside_window`'s ±1 h tolerance, is kept, and — per `get_modes` — belongs to the *next*
  journey window, not this one.
- Stop first → path is `""`, `appendEvent` answers `NO_SESSION`, event echoed and acked only. Gone.

The window between the two is real, not theoretical: `transitionState(STOPPING)` happens
immediately in `stop()`, while the path is cleared only after the recorder task notices
`shouldStop` (up to 250 ms), drains the queue and closes the file.

**On the server**, two events in the same second are a genuine collision — `Mode`'s uniqueness is
`(vessel, started_at)` at second resolution. `collapse_mode_bursts` runs first and folds anything
within 20 s into one row (first timestamp, last mode), so within a single upload this cannot
surface. Across two uploads it can, and `get_or_create` means the **existing** row wins and the new
mode is silently discarded — which is the same rule that makes re-upload safe for reclassified rows.

### Does a failed sidecar write still advance the ack frontier?

**It used to. Fixed in #180.**

It did, because `eventAckFrontier` was assigned from `eventIngest.ackFrontier()` before the apply
loop ran, and `appendEvent`'s return value only reached a `LOG_INFO` — a single `false` covering
both "no session" (benign, expected) and "the write failed" (data loss), so the log line for a full
filesystem claimed the event was "applied while not recording". The next telemetry packet then told
the watch the event was delivered, the watch trimmed it from NVS, and the server never saw it.

Now the frontier means **persisted**. `appendEvent` answers `WRITTEN` / `NO_SESSION` / `FAILED`;
`handleModeEvent` tracks a `settled` seq that follows only the first two, and on a `FAILED` stops
the batch, publishes `settled` and calls `EventIngest::holdFrontierAt(settled)`. A batch that
finishes without a refusal publishes `eventIngest.ackFrontier()` instead, which is `settled` plus
any dead range the ingest jumped (issue #177) — a range no applied event's seq can speak for.

Holding the *published* frontier alone would have deadlocked — the watch's resend would read as a
duplicate and the sidecar would never be offered the event again — so the hold moves the ingest's
own frontier too. Nothing is re-buffered: the watch resends everything unacked once a second, and
re-offers exactly what was never acked. The one event the retry cannot reach is one recovered from
behind a frontier jump: the watch dropped it long ago, so a refusal there is the end of it, and
`Recorder::appendEvent`'s own `ERROR` is all there is to say so.

The knock-on is worth knowing. A sidecar that keeps refusing keeps the watch's queue growing
instead of losing events quietly, and a queue that reaches 64 starts dropping its oldest. Since
#177 that costs the dropped event and nothing more: the next burst declares the new head and the
boat's frontier steps over the hole, where before it froze there for the rest of the session. The
depth is visible on the Control tile either way, and a depth that only climbs is still the signal
that the boat's filesystem is in trouble.

---

## Where each gap is filed

| Question | Outcome | Issue |
|---|---|---|
| Timestamps on a watch that has never had a reference | Stamped 0, dropped at import | #179 |
| Outage drain, frontier holes | Watch declares its queue head; the frontier steps over dead seqs | **fixed, #177** |
| Queue persisted but NVS commit failed | Silent, costs the event on reboot | #178 |
| Mode set while stopped | Never persisted; no baseline segment | #145 |
| Re-upload | Duplicate `Track`, idempotent `Mode`s | #176 |
| Sidecar fetch fails on the device | Uploads without events, track reads "Uploaded" | #174 |
| Sidecar write fails | Frontier held back; the watch resends until it lands | **fixed, #180** |
| Version mismatch | Counted per reason at both ends; the watch's header names both versions | **fixed, #173** |
