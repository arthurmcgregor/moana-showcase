# State machines

The firmware has no single global state machine. State is distributed: each
subsystem owns its own enum (as `std::atomic`), and inter-task changes flow
through the event queue into `AppController::process()`. This file describes the
enums that actually exist in the code. (It replaces an earlier planning
transcript that had drifted from the implementation.)

## RecordingState — `Recorder`

`IDLE → STARTING → ACTIVE → STOPPING → IDLE`, plus `FAILED` from `STARTING`,
`ACTIVE`, or `STOPPING`, and `FAILED → IDLE` (reset). Transition validity is
enforced by `RecordingStateMachine::isValidTransition()` (unit-tested).
`FAILED` is entered when a write/flush fails mid-track or a file can't be opened
(e.g. LittleFS full). The user toggle maps state → action via
`RecordingStateMachine::toggleAction()`: `IDLE`→START, `ACTIVE`→STOP,
`FAILED`→RESET, transitional states→ignored.

## GPSState — `AppController`

`NOT_STARTED → ACQUIRING → LOCKED ⇄ LOST`. Driven by `GPS::dataTask` /
`GPS::healthTask` posting `GPS_STATE` events. Read cross-task (touch, recorder,
web), hence atomic.

## SyncState — `AppController`

`IDLE ⇄ SYNCING`. A sync is started from the Sync screen or boot recovery;
`SYNCING` blocks a second sync and (with `Storage::archiveUsers_`) mutually
excludes with live web archive access. Completion posts `SYNC_COMPLETE`, which
returns it to `IDLE`.

## WifiState — `Connectivity`

`DISCONNECTED / CONNECTING / CONNECTED / ERROR`. (In practice `ERROR` is unused
— failures go straight back to `DISCONNECTED`.)

## ScreenState — `Display`

`MAIN_MENU`, `SPEED_SCREEN`, `DETAIL_SCREEN`, `WIFI_SCREEN`, `SYNC_SCREEN`,
`CALIBRATION_SCREEN`. Transitions are gated by each screen's `canEnter()`;
navigation happens via `ScreenContext::show()/back()`.

## IMUMode — `IMU`

`FUSING / CALIBRATING / CAL_RESULT_PENDING`. Calibration pauses fusion, collects
mag samples, and waits for an explicit apply/discard. Only entered when an IMU
is present and recording is `IDLE`.

## DisplaySleepState — `Display`

`AWAKE ⇄ SLEEPING`. Auto-sleep after an inactivity timeout unless the active
screen requests `keepAwake()`; any touch wakes it.

## RecoveryState — `AppController`

`IDLE / NEEDED`. Set to `NEEDED` at boot when LittleFS looked unclean, driving
a one-shot recovery sync before returning to `IDLE`.
