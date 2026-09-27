# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in `frontend/`.

## Project Overview

The Vue 3 frontend for **Moana**, an ESP32 yacht monitor. It shows GPS tracks on a map with a speed graph, and manages journeys, sailing modes (sail/motor/anchor, …) and event marks (tack, gybe, wildlife, …).

## Commands

`mise run dev` from the repo root runs the whole stack (Django + Vite + DB) in one terminal;
`mise run frontend:dev` runs Vite alone. Both go through `scripts/dev.sh`. Plain npm works too:

```bash
npm run dev          # Dev server (http://localhost:5173)
npm run build        # Production build (type-check + vite build in parallel)
npm run type-check   # TypeScript checking only (vue-tsc --build)
npm run lint         # ESLint with auto-fix
npm run preview      # Preview production build locally
npm run test         # Vitest, single run
npm run test:watch   # Vitest in watch mode
```

### Testing

Vitest 4 with its default config: node environment, and no `vitest.config.ts` — it reads `vite.config.ts`, so the `@/` alias works. Tests sit beside the code in `__tests__/`, fakes in a sibling `__fakes__/` (e.g. `src/map/__fakes__/FakeMapPort.ts`). Covered today: `src/engine/` (timeline engine, cursor, timeline tree, scope and stats, replay clock, contiguous selection), `src/map/` (orchestrator and popup), `src/composables/useJourneySelection` and `src/utils/`. Pure logic is pulled out of components so it can be tested in the node environment. Vue components and API modules have no tests; there is no `@vue/test-utils` or jsdom.

## Environment Setup

Copy `.env.example` to `.env.local` and set:
- `VITE_DEVICE_SERVER_BASE_URL` — the ESP32's URL (default `http://moana.local`)
- `VITE_WEB_SERVER_BASE_URL` — the Django URL (default `http://127.0.0.1:8000`)
- `VITE_MAPBOX_ACCESS_TOKEN` — required for the map

All three are read in `src/config.ts`.

## Architecture

**Stack**: Vue 3 (Composition API `<script setup>`) + TypeScript + Vite 7 + Pinia + `@pinia/colada` + Vue Router 4 + Tailwind CSS 4 + DaisyUI 5

### Dual Backend APIs (`src/api/`)

The app talks to two backends:
- **Device server** (`device.ts`): the ESP32 serves CSV over HTTP on the local network. The app lists device tracks and fetches the raw CSV (`fetchTrackList`/`fetchTrackCsv`), then uploads it straight to the web server — nothing parses it client-side. Device tracks are viewed through the web server after upload.
- **Web server** (`web.ts`): Django REST + PostGIS. Serves GeoJSON directly, with full CRUD for vessels, tracks, journeys, modes, annotations and so on.

`web.ts` produces `TrackGeoJsonData` (in `api/types.ts`) — one format for both map and graph. The `TravelMode` type (`"sailing" | "walking" | "biking" | "driving"`) drives the speed colour scale in `TheMap`.

### State Management (`src/stores/`)

One Pinia store:
- `auth` (`stores/auth.ts`) — current user (`username`, `isAdmin`); `loadUser()` de-dupes the in-flight fetch.

All server data (tracks, vessels, journeys, gear, …) is fetched with `@pinia/colada` queries, in composables (`composables/useQueries.ts`, `useGearQueries.ts`, …) or directly in view components — there is no per-domain store.

### Views and Routing (`src/views/`, `src/router/`)

Routes follow list → detail/map.
- `/tracks/device`, `/tracks/web`, `/tracks/local` — three track-source lists; device/local upload CSV to the web server, then navigate to the map view by web track id
- `/tracks/trace` — trace view
- `/tracks/:id` — one track map view (always fetches web GeoJSON by DB id; there is no `source` param)
- `/journeys` + `/journeys/:id` — list, and the one-screen journey view (map, deck, tree and stats together; `?leg=`/`?seg=` carry the selection). Tabs: Journey | Polar | Photos | Details
- `/wind-rose` — wind rose (from the binned NIWA counts in `public/wind-rose.json`)
- `/weather/upload` — weather upload

### Components (`src/components/`)

The flat top level is shared across views (`TheMap.vue`, `SpeedBarGraph.vue`); `journey/` holds
the journey screen's own parts (rail, deck, tree, dial, readout); `ui/` holds the primitives
listed under Design System below. There is no per-file list here — it went stale twice. Each
component carries a header comment saying what it is for; read that.

### Code Splitting (vite.config.ts)

Heavy dependencies are split into manual chunks: `vue`, `mapbox` (mapbox-gl and its Vue wrapper), `chart` (chart.js and its Vue wrapper).

## Design System

Refined editorial aesthetic: light base (the `moana-light` theme in `app.css`), blue primary accent, Instrument Serif display, mono eyebrow labels. Prefer in-house primitives over raw DaisyUI chrome (`input input-bordered`, `btn btn-primary`, `alert`, `badge`). Heroicons auto-import as `Outline*Icon` / `Solid*Icon` (resolver in `vite.config.ts`) — use icons not emoji.

### Primitives (`src/components/ui/`)

Auto-imported by `unplugin-vue-components`. Use these; do not reinvent or re-style.

- **`<AppField label="...">`** — form row. Renders mono uppercase micro-label, slots the control. Optional `hint` prop.
- **`<input class="ui-input">` / `<textarea class="ui-input ui-input--area">`** — translucent input styling (global `@layer components` in `app.css`). Pair with `AppField` for label.
- **`<AppSelect v-model="...">`** — styled select + trailing chevron. Generic over `T`.
- **`<AppDropzone :icon accept primary secondary @select>`** — drag-drop file picker. Validates against `accept` by default; pass `validate` fn to override. Emits `select` with `File`.
- **`<AppFileChip :file @remove>`** — selected-file display (icon tile + name + formatted size + remove button).
- **`<AppNotice variant="info|success|warning|error">`** — tinted panel with leading icon and body slot. Replaces `alert alert-*`.
- **`<BtnSave :loading :disabled>Label</BtnSave>`** — primary CTA pill. Built-in trailing arrow + inline spinner when `loading`.
- **`<BtnText>Cancel</BtnText>`** — ghost partner button for `BtnSave`.
- **`<ActionGroup>`** + **`<ActionBtn variant="default|danger" :active title>`** — segmented icon-button group. `active` adds teal fill + pulsing dot indicator (no size jump).
- **`<IconSelect v-model :icon :max-width title>`** — 32px-tall pill select with leading icon + trailing chevron. For toolbar controls, not form rows.
- **`<JourneyRef :to :name>`** — pill link to a journey (teal dot + truncated name + arrow nudge). Use for any cross-reference.
- **`.tab-scroll`** — global CSS utility on the `<nav role="tablist">`. Pair tab buttons with `shrink-0 whitespace-nowrap`.

### Layout conventions

- **PageHeader** — self-pads `px-4 md:px-0` so heading aligns with nav brand on mobile (main uses `max-md:p-0`).
- **Detail toolbar** — `<title row> / <control row>` with `flex-wrap`, `basis-full md:basis-auto` + `justify-end` so controls wrap below title on mobile but hug right edge.
- **Height-bound detail screens** — `JourneyScreenView` has no page header at all: the journey's name, its figures and its edit/delete buttons live at the head of the left rail, and the tabs sit over the pane they switch rather than across the screen. No back button either — the nav bar's own links go back. Copy this shape for any screen where the content is the viewport.
- **Nav visibility** — never set `display` in scoped CSS for elements that use Tailwind `hidden`/`md:hidden`/`md:flex` — scoped rules outrank utilities and break responsive toggles. Put the display utility alongside the responsive one (e.g. `inline-flex md:hidden`).

## Data Conventions

- **GeoJSON coords**: `[longitude, latitude]` order (standard GeoJSON)
- **CSV from device**: `(lat, lng)` order — swapped to `(lng, lat)` server-side during Django CSV import (frontend no longer parses device CSV)
- **Speed**: knots
- **Timestamps**: CSV carries either the old `gps_date`/`gps_time` integer fields or an ISO `timestamp` string; the web API uses ISO