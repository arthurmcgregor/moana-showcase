import { computed, onScopeDispose, shallowRef, watch } from "vue";
import type { ShallowRef } from "vue";
import { LngLatBounds } from "mapbox-gl";
import type { Map, Expression  } from "mapbox-gl";
import type { FeatureCollection } from "geojson";
import type { Annotation, Mode, GeoJSONFeatureCollection } from "@/api/web";
import { createTimelineEngine } from "@/engine";
import {
  buildColorScale,
  createMapOrchestrator,
  EMPTY_FC,
  FULL_OPACITY,
} from "@/map/orchestrator";
import { createMapboxAdapter } from "@/map/MapboxAdapter";
import { loadMapIcons } from "@/map/icons";

export interface MapViewProps {
  markers: FeatureCollection;
  travelMode?: string;
  cursorPosition?: [number, number] | null;
  annotations?: Annotation[];
  modes?: Mode[];
  /** The selected leg or segment's window; everything outside it dims. */
  scopeWindow?: { startMs: number; endMs: number } | null;
  /** Journey time the deck's playhead sits at, when a deck owns time. */
  playheadMs?: number | null;
  /** Whether this view has a transport deck at all. */
  hasPlayhead?: boolean;
  /** True while the deck is playing — the camera follows the boat. */
  isPlaying?: boolean;
  /**
   * True while `scopeWindow` is tracking the boat rather than naming a selection.
   * Such a window repaints the dimming and leaves the camera alone.
   */
  following?: boolean;
}

export interface MapViewLayers {
  points: Readonly<Record<string, unknown>>;
  cursor: Readonly<Record<string, unknown>>;
  annotations: Readonly<Record<string, unknown>>;
  modes: Readonly<Record<string, unknown>>;
}

export interface MapViewHandle {
  onMapCreated: (map: Map) => void;
  initialBounds: Readonly<ShallowRef<LngLatBounds | undefined>>;
  layers: MapViewLayers;
}

function buildInitialBounds(markers: FeatureCollection): LngLatBounds | undefined {
  const features = markers.features;
  if (!features || features.length === 0) return undefined;
  const bounds = new LngLatBounds();
  let hasCoords = false;
  for (const f of features) {
    const g = f.geometry;
    if (g.type === "Point") {
      bounds.extend(g.coordinates as [number, number]);
      hasCoords = true;
    } else if (g.type === "LineString") {
      for (const c of g.coordinates) {
        bounds.extend(c as [number, number]);
        hasCoords = true;
      }
    }
  }
  return hasCoords ? bounds : undefined;
}

export function useMapView(
  props: MapViewProps,
  emit: (event: "mapClick", lngLat: [number, number]) => void,
): MapViewHandle {
  const engine = computed(() => {
    const fc = props.markers as GeoJSONFeatureCollection;
    if (!fc?.features?.length) return null;
    const firstTs = fc.features[0]?.properties?.timestamp;
    const lastTs = fc.features[fc.features.length - 1]?.properties?.timestamp;
    if (!firstTs || !lastTs) return null;
    return createTimelineEngine({
      features: fc,
      modes: props.modes ?? [],
      annotations: props.annotations ?? [],
      journeyStart: firstTs,
      journeyEnd: lastTs,
    });
  });

  const initialBounds = shallowRef<LngLatBounds | undefined>(buildInitialBounds(props.markers));

  const orchestrator = createMapOrchestrator({
    markers: () => props.markers,
    travelMode: () => props.travelMode,
    cursor: () => props.cursorPosition,
    annotations: () => props.annotations,
    modes: () => props.modes,
    engine: () => engine.value,
    emitMapClick: (xy) => emit("mapClick", xy),
    raf: (cb) => requestAnimationFrame(cb),
    cancelRaf: (id) => cancelAnimationFrame(id),
    scopeWindow: () => props.scopeWindow ?? null,
    // Passing the getter at all is the signal that a deck owns time, so it is
    // only wired up for views that have one.
    ...(props.hasPlayhead ? { playhead: () => props.playheadMs ?? null } : {}),
    isPlaying: () => props.isPlaying ?? false,
  });

  function onMapCreated(map: Map) {
    const adapter = createMapboxAdapter(map);
    loadMapIcons(adapter);
    orchestrator.attach(adapter);
  }

  const initialColorScale = buildColorScale(props.travelMode) as Expression;

  const layers: MapViewLayers = {
    points: Object.freeze({
      type: "circle",
      source: { type: "geojson", data: EMPTY_FC },
      paint: {
        "circle-radius": 4,
        "circle-color": initialColorScale,
        "circle-opacity": FULL_OPACITY,
      },
    }),
    cursor: Object.freeze({
      type: "symbol",
      source: {
        type: "geojson",
        data: EMPTY_FC,
      },
      layout: {
        "icon-image": "crosshair",
        "icon-size": 1.5,
        "icon-allow-overlap": true,
        "icon-ignore-placement": true,
      },
    }),
    annotations: Object.freeze({
      type: "symbol",
      source: {
        type: "geojson",
        data: EMPTY_FC,
      },
      layout: {
        "icon-image": [
          "case",
          ["==", ["get", "type"], "TACK"], "tack-icon",
          ["==", ["get", "type"], "GYBE"], "gybe-icon",
          ["==", ["get", "type"], "COURSE_CHANGE"], "course_change-icon",
          "note-icon",
        ] as Expression,
        "icon-size": ["case", ["get", "isPending"], 0.8, 1.0] as Expression,
        "icon-allow-overlap": true,
        "icon-ignore-placement": true,
      },
      paint: {
        "icon-opacity": ["case", ["get", "isPending"], 0.6, 1.0] as Expression,
      },
    }),
    modes: Object.freeze({
      type: "symbol",
      source: {
        type: "geojson",
        data: EMPTY_FC,
      },
      layout: {
        "icon-image": [
          "case",
          ["==", ["get", "mode"], "SAIL"], "sail-mode-icon",
          ["==", ["get", "mode"], "MOTORSAIL"], "motorsail-mode-icon",
          ["==", ["get", "mode"], "MOTOR"], "motor-mode-icon",
          ["==", ["get", "mode"], "ROW"], "row-mode-icon",
          ["==", ["get", "mode"], "UNPOWERED"], "unpowered-mode-icon",
          ["==", ["get", "mode"], "ANCHOR"], "anchor-mode-icon",
          ["==", ["get", "mode"], "ALONGSIDE"], "alongside-mode-icon",
          ["==", ["get", "mode"], "BEACHED"], "beached-mode-icon",
          ["==", ["get", "mode"], "TRAILERED"], "trailered-mode-icon",
          "sail-mode-icon",
        ] as Expression,
        "icon-size": ["case", ["get", "isPending"], 0.7, 0.9] as Expression,
        "icon-allow-overlap": true,
        "icon-ignore-placement": true,
      },
      paint: {
        "icon-opacity": ["case", ["get", "isPending"], 0.6, 1.0] as Expression,
      },
    }),
  };

  watch(() => props.markers, () => orchestrator.onMarkersChanged());
  watch(() => props.travelMode, () => orchestrator.onTravelModeChanged());
  watch(() => props.cursorPosition, () => orchestrator.onCursorChanged());
  watch(() => props.annotations, () => orchestrator.onAnnotationsChanged());
  watch(() => props.modes, () => orchestrator.onModesChanged());
  // A window that moved because replay stepped into the next segment must not
  // refit the camera — only a change of selection is deliberate enough for that.
  // `following` rather than `isPlaying` is the difference: the follow outlives
  // pause, and a refit on pause is the same jolt as a refit mid-replay.
  watch(
    () => props.scopeWindow,
    () => {
      if (props.following) orchestrator.onScopeWindowChanged();
      else orchestrator.onScopeChanged();
    },
  );
  watch(() => props.playheadMs, () => orchestrator.onPlayheadChanged());

  onScopeDispose(() => orchestrator.dispose());

  return { onMapCreated, initialBounds, layers };
}
