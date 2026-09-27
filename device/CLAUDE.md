# Device

PlatformIO + Arduino-framework firmware for the M5Stack CoreS3. Nothing happens in `loop()`:
`src/main.cpp` instantiates a global `App` and calls `app.init()`, after which every subsystem runs
in its own FreeRTOS task and communicates only by posting typed events
(`include/events/app_event.h`) through `App::postEvent()`.

## Build, flash, test

All commands run from `device/`. `m5stack-cores3` is the `[platformio] default_envs`, so a bare
`pio run` builds the firmware.

```bash
pio run --target uploadfs   # LittleFS image — the only path for data/ (incl. the Vue app)
pio test -e native          # host googletest suite, sub-second, no board needed
pio test -e device-test     # on-hardware LittleFS round-trip, needs a flashed board
```

CI runs `pio test -e native` and a full `pio run` on every push to `main`, but commits land on
`main` directly (see the root `CLAUDE.md`) — so CI reports a regression, it does not block one.
Run the native suite before every commit anyway; it is sub-second.

`[env:native]` is an explicit `build_src_filter` allowlist compiled against the shims in
`test/stubs/` (Arduino, Logger, StatusBar). Add a `.cpp` to a tested module and it must go in that
filter too or it is silently left out of the test build — and a module stays reachable only while it
keeps FreeRTOS and M5 APIs behind an injected port.

**Standalone bench/recovery sketches — not dead code.** Each `*_sketch.cpp` at the `device/` root
has its own PlatformIO env and builds only when that env is selected, so none of them appear in the
default build. Don't delete one for looking unreferenced. `imu_sketch` shares the `magcal` NVS
namespace with production, so it can seed or inspect the mag calibration the firmware reads.

## Architecture

`App` (`include/app.h`, `src/app.cpp`) is the central coordinator, owning the subsystems, the
GPS/IMU hardware, and the `AppController` that holds the state machine:

```
App
 ├─ Display (ui/)                   — SPI mutex, status bar, screens, touch, sleep/wake
 ├─ Recorder (recording/)           — GPS fixes to LittleFS, own queue and task
 ├─ Connectivity (connectivity/)    — WiFi, AsyncWebServer serving the app and track endpoints
 ├─ EspnowTelemetry (connectivity/) — ESP-NOW link to the watch
 ├─ Storage (storage/)              — SD card sync, boot recovery, track utilities
 ├─ GPS (hardware/)                 — TinyGPSPlus over HardwareSerial
 ├─ IMU (hardware/)                 — BMI270 + BMM150 fused (Mahony) for heel/trim/heading
 └─ AppController (events/)         — owns GPS/sync/recovery state; processes events
```

`App::init()` spawns one task per concern (see the `xTaskCreate` calls in `src/app.cpp`).
`appControllerTask` drains the event queue into `AppController::process()`, which serialises every
state change onto that one task. `IMU::fusionTask` is the only conditional spawn — it starts just
when `IMU::isPresent()`.

### Ports and seams

Hardware and filesystem access sit behind injected interfaces, which is what makes most of this tree
testable off-board: `Display` takes `DisplayHardware`/`TouchInput`/`Scheduler`
(`include/ui/ports/`) in its constructor, `Storage` takes a `TrackFileSystem`, and `AppController`
sees only the lambda structs declared in `include/events/app_controller.h` — never `App` itself.
Production adapters wrap M5Unified, LittleFS and SD; fakes live in `test/test_native/fakes/` and
`test/test_native/fake_filesystem.h`.

### Events from the watch

Commands from the watch arrive over ESP-NOW through `EspnowTelemetry`'s receive callback and are
posted as ordinary events, so `AppController` keeps state-machine ownership either way:
`SET_RECORDING` is an absolute recording on/off, `MODE_EVENT` a timestamped sailing-mode change.
Applied mode events also reach the active track's `.events.jsonl` sidecar through the Recorder, and
sidecars ride the SD sync with their tracks.

The ack frontier means **persisted**, not merely parsed (issue #180). `Recorder::appendEvent`
answers with an `EventPersist`, and `AppController::handleModeEvent` advances the frontier only over
events that reached the sidecar or had no session to reach — `NO_SESSION` is a deliberate ack (with
no track there is nothing to attach the event to), `FAILED` stops the batch and holds the frontier
where it was. The hold moves `EventIngest`'s own frontier too, not just the published one: leave it
forward and the watch's resend reads as a duplicate and the sidecar is never offered the event
again. Nothing needs re-buffering — the watch resends everything unacked once a second.

The receive callback validates `version` before anything else, off byte 0 — the one field at a
fixed offset in every version of every packet — and only then dispatches on frame length. The gate
is `moana::checkFrameVersion` in `shared/espnow_protocol.h`, so this end and the watch's run the
same rule rather than two copies of it. Rejected frames are counted per reason in
`EspnowTelemetry::rxStats()` and logged, because a silent rejection is indistinguishable from a
watch that is switched off (issue #173): a rate-limited `LOG_WARN` for a version mismatch, naming
both versions, and a separate, quieter `LOG_DEBUG` for stray traffic on our own version — separate
timers, so a noisy channel cannot suppress the warning a person has to act on.

`AppController` owns the two pieces of state echoed back in every telemetry packet. Both are pure
logic, unit-tested natively, and their headers carry the reasoning — read those before changing
either:
- `EventIngest` (`include/events/event_ingest.h`) — dedupe and contiguous-ack frontier for
  `MODE_EVENT`. The frontier is RAM-only while the watch's seq counter persists in NVS, so after a
  boat reboot every seq the watch sends looks like one giant hole. Each `Event` carries the head of
  the watch's queue (`oldest_unacked_seq`, issue #177), which settles that on the first packet and
  also frees the frontier when the watch has *dropped* a seq on overflow — the case that used to
  freeze delivery until the boat was rebooted. It never infers a gap; it is told.
- `DistanceAccumulator` (`include/recording/distance_accumulator.h`) — cumulative session metres,
  integrated only while recording is ACTIVE, anchor dropped on fix loss so a dropout never adds a
  straight-line jump. The watch derives leg and session distances purely from this echo.

### State

There is no global state machine. State is distributed — each subsystem owns its own enum as a
`std::atomic`, and cross-task changes flow through the event queue. `STATE_MACHINE.md` covers all of
them: which subsystem owns each, the legal transitions, and what enters each state.

### Display (`include/ui/display.h`, `src/ui/display.cpp`)

`Display` is the single deep module for every display concern, screen transition validation and
touch routing included. `AppController` reaches it through one entry point,
`Display::apply(const DisplayCmd&)` — a tagged union of every render-side instruction
(`include/ui/display_cmd.h`). The typed methods behind it (`updateStatusBar`, `showSyncResult`, …)
are not the controller's interface.

Screens derive from a virtual `Screen` base (`include/ui/screens/screen.h`) and are held in a registry (`screens_` array keyed by `ScreenState`); Display dispatches polymorphically via `activeScreen()`, not `switch` statements. Each screen opts into what it needs by overriding virtuals — rendering, click and hold, GPS data, ticks, entry validation, state-change hooks.

Screens receive a `ScreenContext*` at init (not `App*`). `ScreenContext` is their only collaborator: navigation (`show`/`back`) calls Display directly on the touch task, while commands (`toggleRecording`/`requestSync`/…) post `BUTTON_PRESS` events so `AppController` keeps state-machine ownership. Screens cannot reach into subsystems.

**Every font selection goes through `ui::applyTextStyle()`** (`include/ui/text_style.h`) — never
`M5.Display.setTextSize()`, and never a bare `setFont()`, which desyncs the style cache. Each named
style maps to a Montserrat VLW font baked at that pixel size (`src/ui/fonts/`, generated by
`tools/fonts/build.sh` from the same frozen TTF the watch uses); the header explains why scaling one
instead is not an option. That font data is ~190 KB and the bulk of the firmware, which is why
`Speed` (136 px) carries digits only.

**All display writes must hold the SPI lock.** Display's public methods take it internally; external callers use `Display::acquireSPI()` (returns an RAII `Display::SPIGuard`) or the raw `lockSPI()`/`unlockSPI()`. The underlying mutex is **recursive**, so a touch handler that re-enters Display (e.g. a tap that navigates) does not self-deadlock. Shared with SD access on the FSPI bus (see `Storage::setSpiLock`).

### Hardware

- **GPS** (`include/hardware/gps.h`): wraps TinyGPSPlus over HardwareSerial. `dataTask` continuously reads NMEA and posts data events; `healthTask` monitors fix staleness.
- **IMU** (`include/hardware/imu.h`): BMI270 + BMM150 fused via Mahony at 100 Hz (`fusionTask`) for heel/trim/heading, appended to recorded CSV rows. In-situ mag calibration (`CALIBRATING` → `CAL_RESULT_PENDING` → apply/discard) persists hard/soft-iron offsets to NVS. `isPresent()`, set in `init()`, gates whether the fusion task is spawned at all.

### Storage and logging

LittleFS is the primary filesystem (fast writes, no SPI contention with the display); the SD card is
used only for sync and export. **LittleFS must be mounted before Logger or Recorder are
initialised.**

- **Recorder** (`include/recording/recorder.h`): batches GPS fixes to LittleFS off its own FreeRTOS queue and task.
- **Storage** (`include/storage/storage.h`): pure I/O module for SD card sync, LittleFS track listing, boot recovery flags, and filesystem formatting. Has no delegate — App owns sync/recovery state and orchestration. `startSync()` takes a completion callback; the callback posts a `SYNC_COMPLETE` event back to App's event queue. `TrackFilename` (`include/storage/track_filename.h`) validates and normalises track names.
- **Logger** (`include/utils/logger.h`): singleton, **asynchronous** — `LOG_DEBUG`/`LOG_INFO`/`LOG_WARN`/`LOG_ERROR(tag, ...)` filter by level, format into a stack buffer, and push onto a FreeRTOS queue, so no caller ever blocks on flash I/O; a background `LogWriter` task drains to LittleFS and rotates by size. `LOG_ROTATE()` rotates synchronously (Storage copies rotated logs to SD during sync).

## Conventions

- Subsystems receive `App*` via an `init(App* app)` method, not the constructor — `App`'s members are constructed before `init()` runs.
- Static task functions follow the pattern `static void MyClass::myTask(void* pvParameters)` with `App* app = static_cast<App*>(pvParameters)`.
- `LOG_TAG` is a file-scoped `static const char*` in each `.cpp` file.
- C++17 (`-std=gnu++17`) throughout; the firmware env also builds with `-Wall -Wextra`.
