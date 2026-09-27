# Moana

Yacht monitoring system: an ESP32-S3 boat unit that records GPS tracks and serves them over
WiFi, a T-Watch Ultra wrist companion, and a Django + Vue web stack for storing and
visualising journeys.

| Area | What it is |
|---|---|
| `device/` | CoreS3 boat-unit firmware — GPS recording, on-screen readouts, WiFi web server |
| `watch/` | T-Watch Ultra companion firmware — sailing display and remote control |
| `shared/` | C++ headers compiled into both firmwares (`-I ../shared`) |
| `server/` | Django REST API + PostGIS |
| `frontend/` | Vue 3 web app, also served off the device's LittleFS |
| `tools/`, `docs/` | Font generation; hardware reference; the watch event traces (`docs/event-trace.md`) |

`device/`, `watch/`, `server/` and `frontend/` each carry their own `CLAUDE.md`. `mise` is the
task runner (`mise tasks`); `server/mise.toml` is a second, directory-scoped set, so `mise run
test` resolves only from inside `server/`.

## Git workflow

Solo project: **commit straight to `main`**. No feature branch, no pull request, no review gate
unless there is a reason to want one (a long risky change worth parking, or work that needs to be
shown before it lands). Branches that exist for ceremony alone just age.

`.github/workflows/ci.yml` runs on push to `main`, so work that lands straight there goes red
after the fact rather than being gated before. It also runs on `pull_request` and
`workflow_dispatch`, so a parked branch does get checked. Run the suite of whatever you touched
before pushing — `pio test -e native` from `device/` or `watch/`, `server/scripts/run-tests.sh`
(any CWD), `npm run test` from `frontend/`.

## Running the stack

```bash
mise run dev:setup   # once per clone: env files, Python venv, npm packages
mise run dev         # database + Django (:8000) + Vite (:5173); Ctrl-C stops all
```

`scripts/dev.sh` is behind all of it, and takes the stack apart too — `dev:db`, `server:dev`
and `frontend:dev` run one piece each. `dev:down` stops the database, which `dev` deliberately
leaves running between sessions.

**You are an admin locally without doing anything.** Authelia's `Remote-User` header supplies
the staff identity in production; locally `DevRemoteUserMiddleware` sets that header itself, so
the staff user exists on the first request and there is no login form. Inert unless `DEBUG`.
`server/CLAUDE.md` owns the mechanism, the `/admin/` password and how to switch it off.

Three compose files, easy to confuse:

| File | Project | What it is |
|---|---|---|
| `server/docker-compose.yml` | `moana-dev` | The dev database. PostGIS on **5436**, persistent volume |
| `server/docker-compose.test.yml` | `moana-test` | Throwaway test DB on **5434**, tmpfs (`mise run test`) |
| `docker-compose.yml` (root) | *directory name* | Production-shaped stack — builds the Django and nginx images from `prod.env`. Not for local work |

`server/.env` is the single source of truth for the dev database: the compose file
interpolates the same `POSTGRES_*` values that host-side Django reads, so moving
`POSTGRES_PORT` moves both ends at once. 5436 rather than 5432 because 5432 is contested on
most machines.

## Deploying the frontend to the device

The device serves the Vue app off LittleFS, so a frontend change reaches it only after a
filesystem upload:

```bash
mise run frontend:build
mise run frontend:copy               # frontend/dist → device/data/frontend/
cd device && pio run --target uploadfs
```

## Data flow

GPS fixes → `Recorder` → CSV on device LittleFS (plus a JSONL events sidecar carrying the
watch's mode changes) → `POST /api/tracks/upload-csv/` → PostGIS → GeoJSON to the frontend.
The watch reaches the server only through the boat unit, never directly.

That is the happy path. `docs/event-trace.md` walks the two paths the watch owns — a mode tap
becoming a `Mode` row, and hold-to-record becoming a state echo — function by function, and
answers what happens at each seam when something fails: a link outage and the queue draining at
once, a mode set while recording is stopped, a stop and a mode arriving in the same second, a
re-upload, a sidecar write that fails. Read it before changing anything on either path; the
failure modes live between the modules, not inside them.

## What moves in lockstep

Three things are duplicated across the stack and have to be edited together. A one-sided change
fails silently, at runtime, in production — so two of the three now have a mechanical check
standing over them, named below. The third has none.

**The wire format.** `shared/espnow_protocol.h` is the ESP-NOW packet layout, compiled into both
firmwares via `-I ../shared`. Change it in `device/` and `watch/` together and bump
`kProtocolVersion`, or the two ends disagree about what a packet means. Every wire struct's size
is pinned by a `static_assert` in the header, so a layout change breaks both builds until the
number is updated deliberately — the reminder to bump `kProtocolVersion` sits in that assert's
message, since nothing can check the bump itself. Both receive callbacks read `version` off byte 0
first — the one field at a fixed offset in every version of every packet, so a bump that changes a
struct's *size* is still recognised as a bump rather than thrown away as a stray frame (issue
#173) — and only then dispatch on frame length before reading `type`, which is why an assert also
holds `sizeof(Command) != sizeof(Event)`.
The two `platformio.ini` files pin espressif32 separately, and neither pin moves without the
other — reasons in the comment at the top of `device/platformio.ini`.

**Sailing-mode names**, one vocabulary spanning `shared/` and `server/`: the `SailingMode` enum
and `sailingModeName()` in `shared/espnow_protocol.h`, `MODE_KINDS` in
`server/moana/tracks/events_jsonl.py`, and `Mode.MODE_CHOICES` in `tracks/models.py` — which
needs a migration when it changes (see `0013_alter_mode_mode.py`). `tests/test_events_jsonl.py`
asserts the two Python halves match, and `tests/test_wire_vocabulary.py` parses the header to hold
the C++ names, their order and their enum values against both — so all three definitions are
checked, by the server suite rather than by either firmware build.

**Mis-press debounce**, one duration in two places: `kMinLegMs` in `watch/src/stats_engine.h`
and `MODE_DEBOUNCE_SECONDS` in `server/moana/tracks/events_jsonl.py`. A correcting press inside
that window relabels the watch's current leg and collapses into the previous event on import —
both retroactively, so nothing on the wire, in the queue or in the sidecar is delayed or dropped,
and a re-upload re-derives the segments. Move one and the wrist and the web map disagree about
where a leg began. Nothing checks this pair — the C++ side is a compile-time constant in a header
no Python test reads yet.

## Coordinate conventions

Coordinates are handled differently across the stack — get this wrong and tracks will appear in the ocean:
- **LittleFS CSV / device output**: `(lat, lng)` order
- **Django PostGIS storage**: `Point(longitude, latitude)` — CSV import swaps the order
- **GeoJSON everywhere**: `[longitude, latitude]` (standard GeoJSON)
