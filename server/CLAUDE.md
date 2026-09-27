# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Django REST API for tracking sailing vessel journeys with GPS data, crew records, operating modes, and annotations. Uses PostGIS for geographic data and Django REST Framework for the API layer.

**Stack**: Django 6.0, Django REST Framework, PostgreSQL 18.1 + PostGIS, Python 3.14

## Commands

### Running it

From the repo root, `mise run dev` brings up the database, Django and the frontend together;
`mise run server:dev` is the backend half alone (it starts the database and migrates first).
Both go through `scripts/dev.sh` — see the root `CLAUDE.md` for how the three compose files
differ. The dev database is `docker-compose.yml` here: PostGIS on **5436**, persistent volume,
project `moana-dev`.

`DJANGO_LOG_SQL=1` puts `django.db.backends` back to DEBUG, printing every query and its
parameters — worth it when chasing an N+1, unreadable otherwise (one page load buried the
runserver output, which is why it is off by default).

### Who you are locally

Identity has **one** path in every environment: `tracks.middleware.RemoteUserMiddleware` reads
`Remote-User` / `Remote-Groups`, gets-or-creates that user, and sets `is_staff` from
`moana_admin` group membership — but **only** on requests that also carry `X-Proxy-Auth`
matching `REMOTE_USER_PROXY_SECRET`. The header alone proves nothing: Caddy rewrites
`Remote-User` itself so it cannot be forged *through* the proxy, but anything that reaches
Django's own port skips Caddy entirely, and the shared secret is what makes that route useless.
`settings_prod` reads the secret with `os.environ[...]`, so a deployment missing it fails to
boot rather than trusting a default; an empty secret at runtime trusts nobody and the site goes
read-only. Deployed it lives in `secret.env`, and the proxy is configured to send the same value;
locally the default in `settings.py` is fine.

`RemoteUserMiddleware` is in the base `MIDDLEWARE`, and inert without the headers.
Deployed, Authelia sets them. Locally nothing does, so
`tracks.middleware.DevRemoteUserMiddleware` sits in front of it and sets them itself —
`DEV_ADMIN_USERNAME` plus `moana_admin` — and the real middleware does the rest. That is what
makes `/api/me/` answer `is_admin: true` and the Vue app show its editing controls.

Faking the header rather than calling `auth.login()` is deliberate: the production identity code
runs locally too (so its group→staff logic is exercised daily), no session rows are written for
cookie-less clients like curl or the boat unit, and the csrftoken cookie keeps coming from
`@ensure_csrf_cookie` on `/api/me/` exactly as it does deployed. The cost is that you are always
the header user — there is no session to log out of — which is also true in production.

A request that names its own `Remote-User` wins, and brings its own `Remote-Groups`, so
`curl -H 'Remote-User: crew' …` is how you see the site as a non-admin — no `X-Proxy-Auth`
needed locally, because standing in for the proxy means vouching for the request whoever it
claims to be. `tracks/tests/test_remote_user.py` is the deployed half of that boundary.

`manage.py devadmin` is now a convenience, not a prerequisite: the account exists the moment the
first request goes past. What the command adds is a known password from `DEV_ADMIN_PASSWORD`
(`admin`/`admin`) and `is_superuser`, for `/admin/` with the shim switched off. `scripts/dev.sh`
runs it after every migrate; `mise run dev:admin` does it alone.

Three things keep the shim out of a deployment: `settings.py` only adds it when `DEBUG`,
`settings_prod` strips it from `MIDDLEWARE` again — load-bearing, because `settings.py` builds
that list while `DEBUG` is still `True` — and both the shim and the command check
`settings.DEBUG` at runtime. `DEV_AUTO_LOGIN=0` turns the shim off locally for the anonymous,
read-only view. `tracks/tests/test_dev_admin.py` covers all of it, the `settings_prod` strip
included.

`settings_prod` rebinds `MIDDLEWARE` and `REST_FRAMEWORK` rather than mutating them: `from
.settings import *` binds the base module's own list and dict, so an in-place `insert()` or
`[key] =` edits the dev settings too.

### Testing

Tests run against a dedicated, ephemeral PostGIS database (docker-compose.test.yml)
on host port **5434** — separate from the dev DB, and from the other local Postgres
instances that make 5432 unusable. `scripts/run-tests.sh` starts it, points Django
at it via `POSTGRES_*` overrides, and runs pytest with the venv interpreter. The DB is
kept running between invocations for fast reruns.

```bash
# From server/ — the reliable entrypoint (starts the test DB if needed):
mise run test                              # all tests
scripts/run-tests.sh                       # equivalent
scripts/run-tests.sh tracks/tests/test_models.py       # a specific file
scripts/run-tests.sh --snapshot-update     # regenerate snapshots after API changes
mise run test:down                         # stop & remove the ephemeral test DB
```

Bare `pytest` (run from `moana/`) also works, but only when `POSTGRES_*` point at a
running PostGIS DB — the script is preferred because it guarantees that.

DB connection params (`settings.py`) come from `POSTGRES_{DB,USER,PASSWORD,HOST,PORT}`,
loaded from `server/.env` by mise, defaulting to the local-dev values. There is no
`CreateExtension` migration, so PostGIS is installed into `template1` in the test DB
(`initdb-test.sh`) — that is what lets Django's freshly-created `test_moana` inherit it.

Other commands (run from the `moana/` subdirectory where `manage.py` lives):

```bash
cd moana

# Run dev server
python manage.py runserver 0.0.0.0:8000

# Legacy Django test runner (needs POSTGRES_* pointing at a PostGIS DB)
python manage.py test

# Migrations
python manage.py makemigrations
python manage.py migrate
```

Dev database on its own (equivalent to `mise run dev:db` from the repo root):
```bash
docker compose -f docker-compose.yml up -d --wait
```


## Models

All models are anchored to **Vessel**, except `Person` (standalone) and `WeatherStation`/`WeatherObservation` (standalone weather data):

- **Track** / **TrackPoint** — GPS recording sessions with individual points (`PointField`, geography=True)
- **Mode** — vessel operating state (SAIL, MOTORSAIL, MOTOR, ROW, UNPOWERED, ANCHOR, ALONGSIDE, BEACHED, TRAILERED) with only `started_at`; end time is implied by the next mode's start, or capped by a journey boundary
- **Annotation** — point-in-time events (TACK, GYBE, REEF, SHAKE_REEF, LOAD/UNLOAD_PASSENGERS, LOAD/UNLOAD_GEAR, WILDLIFE, WEATHER, WAYPOINT, MOB, HEAVY_WEATHER_PREP, NOTE)
- **Journey** / **Leg** — temporal containers grouping tracks, modes, annotations
- **Aboard** / **Person** — crew tracking with time ranges
- **WeatherStation** / **WeatherObservation** — standalone weather data (wind, temperature, rain, humidity, tide)

Key design patterns:
- **Implied end times**: Modes have no `ended_at` field. `get_ended_at()` derives the end from the next mode, falling back to track end time. When serialized in a journey context, an `end_boundary` parameter caps this to the journey's end time.
- **Computed statistics**: `get_duration()`, `get_statistics()`, `get_tracks()` etc. are calculated on-demand, not stored. `statistics.JourneyStatisticsService` handles distance (nautical miles via PostGIS), duration, and per-mode breakdowns.
- **GIS coordinates**: Stored as PostGIS Point in (longitude, latitude) order. CSV upload expects (lat, lng) and swaps them. GPS CSV parsing lives in `gps_csv.py` (`parse_gps_csv`, `parse_gps_date`, `parse_gps_time`, `parse_gps_datetime`).
- **Journey preview images**: Static map PNGs generated via `previews.py` (matplotlib+contextily+geopandas). Cached in `MEDIA_ROOT/previews/{journey_id}_{zoom}.png`. Signals auto-regenerate/invalidate on Journey save/delete and Track changes.
- **Serializer duality**: List actions use `TrackSerializer`/`JourneySerializer`; `retrieve` uses `TrackDetailSerializer`/`JourneyDetailSerializer` (with nested points/legs). `ModeSerializer` reads `end_boundary` from serializer context to compute `ended_at` and `duration`.

## API Structure

All endpoints under `/api/tracks/` using DRF ModelViewSets with full CRUD:

- `/vessels/`, `/people/`, `/tracks/`, `/modes/`, `/annotations/`, `/aboard/`, `/journeys/`, `/legs/`
- `/weather-stations/`, `/weather-observations/`

Notable custom actions:
- `POST /tracks/upload-csv/` — GPS CSV import (columns: `lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time`; date DDMMYY, time HHMMSScc)
- `GET /journeys/{id}/modes/` — modes within journey timeframe
- `GET /journeys/{id}/annotations/` — annotations within journey timeframe
- `GET /journeys/{id}/aboard/` — aboard records overlapping journey timeframe
- `GET /journeys/{id}/tracks/` — GeoJSON track points within journey timeframe; optional `?weather_station=<id>` joins nearest weather obs (±5 min)
- `GET /journeys/{id}/preview/` — PNG thumbnail (cached); `?zoom=region` (default) or `?zoom=detail`
- `GET /tracks/{id}/geojson/` — GeoJSON for a single track; optional `?weather_station=<id>`
- `POST /weather-stations/{id}/upload-csv/` — weather data import (Time column in NZ local time, auto-converted to UTC)

Common filter params: `vessel=<id>`, `started_after`, `started_before` on most time-based endpoints; `?ongoing=true` on journeys; `?current=true` on aboard.

## GIS Setup (macOS)

`settings.py` sets `GDAL_LIBRARY_PATH` and `GEOS_LIBRARY_PATH` for Homebrew (`/opt/homebrew/lib/`), overridable via environment variables. PostGIS extension must be manually enabled in the database: `CREATE EXTENSION postgis;`

## Python Environment

Uses `mise` for Python version management (3.14.2) with a `.venv` virtualenv. Activate with `source .venv/bin/activate` or rely on mise's auto-activation.
