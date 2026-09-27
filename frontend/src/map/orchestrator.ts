import type { FeatureCollection, Feature } from "geojson";
import type { Annotation, Mode } from "@/api/web";
import type { TimelineEngine } from "@/engine";
import type {
  MapPort,
  AnimationControlModel,
  PaintValue,
  Unsubscribe,
} from "./MapPort";
import { renderAnnotationPopup, renderModePopup } from "./popups";
import { SPEED_COLORS, speedBucket, thresholdsFor } from "./speedScale";

export const LAYER = {
  POINTS: "points",
  CURSOR: "cursor",
  ANNOTATIONS: "annotations",
  MODES: "modes",
} as const;

export const EMPTY_FC: FeatureCollection = { type: "FeatureCollection", features: [] };

export function buildColorScale(mode: string | undefined): PaintValue {
  const thresholds = thresholdsFor(mode);
  const expr: unknown[] = ["interpolate", ["linear"], ["get", "speed"]];
  for (let i = 0; i < thresholds.length; i++) {
    expr.push(thresholds[i], SPEED_COLORS[i]);
  }
  return expr;
}

const SPEED_STEPS = [1, 2, 5, 10, 25, 50, 100];

/** Isolate, not an age ramp: one flat floor, so a faint dot has one meaning. */
export const DIMMED_OPACITY = 0.18;
export const FULL_OPACITY = 0.8;

/**
 * Dimming asks a question of each fix — "are you inside the selection?" — and the
 * honest place for it is the paint expression, because the answer changes far
 * more often than the track does. Each fix therefore carries `tMs`, the one fact
 * that never changes, and the window lives in the expression.
 *
 * Baking the answer into the source instead meant every change of window rebuilt
 * every feature: two object spreads and a `Date` parse per fix, then a full
 * source write. Fine for a click, ruinous once replay started moving the window
 * at every segment boundary.
 */
export function buildScopeOpacity(window: { startMs: number; endMs: number } | null): PaintValue {
  if (!window) return FULL_OPACITY;
  // An open-ended scope has no upper bound to compare against.
  const endMs = Number.isFinite(window.endMs) ? window.endMs : Number.MAX_SAFE_INTEGER;
  return [
    "case",
    [
      "all",
      [">=", ["coalesce", ["get", "tMs"], -1], window.startMs],
      ["<=", ["coalesce", ["get", "tMs"], -1], endMs],
    ],
    FULL_OPACITY,
    DIMMED_OPACITY,
  ];
}

export interface OrchestratorInputs {
  markers: () => FeatureCollection;
  travelMode: () => string | undefined;
  cursor: () => [number, number] | null | undefined;
  annotations: () => Annotation[] | undefined;
  modes: () => Mode[] | undefined;
  engine: () => TimelineEngine | null;
  emitMapClick: (xy: [number, number]) => void;
  raf: (cb: () => void) => number;
  cancelRaf: (id: number) => void;
  /**
   * The selected scope's time window, or null for the whole journey. When a
   * scope is set the camera fits it and everything outside drops to the floor.
   */
  scopeWindow?: () => { startMs: number; endMs: number } | null;
  /**
   * Present when a transport deck owns time. The points-reveal animation and
   * its map button are then suppressed — two ways to animate the same track
   * would fight each other.
   */
  playhead?: () => number | null;
  /** True while the deck is playing; the camera then follows the boat. */
  isPlaying?: () => boolean;
}

export interface MapOrchestrator {
  attach(port: MapPort): void;
  onMarkersChanged(): void;
  onTravelModeChanged(): void;
  onCursorChanged(): void;
  onAnnotationsChanged(): void;
  onModesChanged(): void;
  onScopeChanged(): void;
  /**
   * The scope's window moved without the selection changing — replay following
   * the boat from one segment to the next. Repaints the dimming and leaves the
   * camera alone: a fit per segment would yank the view about once a minute,
   * and replay's whole contract is that it pans and never refits.
   */
  onScopeWindowChanged(): void;
  onPlayheadChanged(): void;
  setAnimationEnabled(on: boolean): void;
  isAnimationEnabled(): boolean;
  getAnimationSpeed(): number;
  dispose(): void;
}

export function createMapOrchestrator(inputs: OrchestratorInputs): MapOrchestrator {
  let port: MapPort | null = null;
  let sourceInitialized = false;
  const subscriptions: Unsubscribe[] = [];

  let animationEnabled = false;
  let animationSpeed = 10;
  let progress = 0;
  let lastRenderedProgress = -1;
  let rafId: number | null = null;
  const animationListeners = new Set<() => void>();

  function notifyAnimationChange() {
    for (const cb of animationListeners) cb();
  }

  function buildAnimationControlModel(): AnimationControlModel {
    return {
      isEnabled: () => animationEnabled,
      getSpeed: () => animationSpeed,
      toggle: () => setAnimationEnabled(!animationEnabled),
      cycleSpeed: () => {
        const idx = SPEED_STEPS.indexOf(animationSpeed);
        animationSpeed = SPEED_STEPS[(idx + 1) % SPEED_STEPS.length];
        notifyAnimationChange();
      },
      onStateChange: (cb) => {
        animationListeners.add(cb);
        return () => {
          animationListeners.delete(cb);
        };
      },
    };
  }

  function stopRaf() {
    if (rafId !== null) {
      inputs.cancelRaf(rafId);
      rafId = null;
    }
  }

  /**
   * Every feature with `tMs` stamped on, and the point subset the source is made
   * of. Rebuilt only when the track itself changes: parsing a timestamp per fix
   * is the expensive part, and the answer never changes, so it is paid once per
   * track rather than once per window. Two things ask "is this fix inside the
   * selection?" on every change of scope — the dimming, through the paint
   * expression, and the camera fit — and neither should be reparsing to find out.
   *
   * The fit needs the line geometry the source does not, which is why the two
   * lists are kept rather than one.
   */
  let preparedFeatures: Feature[] = [];
  let preparedPoints: Feature[] = [];

  function preparePoints() {
    preparedFeatures = (inputs.markers().features ?? []).map((f) => {
      const timestamp = f.properties?.timestamp;
      const ms = typeof timestamp === "string" ? new Date(timestamp).getTime() : NaN;
      return {
        ...f,
        properties: { ...f.properties, tMs: Number.isNaN(ms) ? null : ms },
      };
    });
    preparedPoints = preparedFeatures.filter((f) => f.geometry?.type === "Point");
  }

  function writePointSlice(features: Feature[], displayCount: number) {
    if (!port) return;
    // Dedupe only during playback: a paused rAF stepping the same count twice
    // should not repaint. When animation is off, animatePoints() resets
    // lastRenderedProgress to -1 so the full-dataset write always lands.
    if (displayCount === lastRenderedProgress && animationEnabled) return;
    lastRenderedProgress = displayCount;
    port.setSourceData(LAYER.POINTS, {
      type: "FeatureCollection",
      features: features.slice(0, displayCount),
    });
  }

  /**
   * Nothing selected means nothing dimmed — the flat floor only exists to
   * separate a selection from the rest of the day. This is the whole cost of a
   * scope change now: one paint property, no source write.
   */
  function updateScopeOpacity() {
    if (!port || !port.hasLayer(LAYER.POINTS)) return;
    port.setPaintProperty(
      LAYER.POINTS,
      "circle-opacity",
      buildScopeOpacity(inputs.scopeWindow?.() ?? null),
    );
  }

  function animatePoints() {
    stopRaf();
    const features = preparedPoints;
    const total = features.length;

    progress = 0;
    lastRenderedProgress = -1;

    if (!animationEnabled) {
      writePointSlice(features, total);
      progress = total;
      return;
    }

    const step = () => {
      progress += animationSpeed;
      if (progress > total) progress = 0;
      writePointSlice(features, Math.min(Math.floor(progress), total));
      rafId = inputs.raf(step);
    };
    step();
  }

  function zoomToMarkers() {
    if (!port) return;
    const window = inputs.scopeWindow?.() ?? null;
    const coords: Array<[number, number]> = [];
    for (const f of preparedFeatures) {
      const g = f.geometry;
      if (!g) continue;
      // Selecting fits the camera to the selection, so only its coordinates
      // count towards the bounds.
      if (window) {
        const ms = f.properties?.tMs;
        if (typeof ms !== "number" || ms < window.startMs || ms > window.endMs) continue;
      }
      if (g.type === "Point") {
        coords.push(g.coordinates as [number, number]);
      } else if (g.type === "LineString") {
        for (const c of g.coordinates) coords.push(c as [number, number]);
      }
    }
    if (coords.length === 0) return;
    port.fitBounds(coords, 50, 0);
  }

  function snapOverlay<T>(
    items: T[] | undefined,
    engine: TimelineEngine | null,
    getTimestamp: (item: T) => string,
    buildProps: (item: T) => Record<string, unknown>,
  ): FeatureCollection {
    if (!items || !engine) return EMPTY_FC;
    const features: Feature[] = [];
    for (const item of items) {
      const snapped = engine.snapToPoint(new Date(getTimestamp(item)).getTime());
      if (!snapped) continue;
      features.push({
        type: "Feature",
        geometry: {
          type: "Point",
          coordinates: snapped.feature.geometry.coordinates,
        },
        properties: buildProps(item),
      });
    }
    return { type: "FeatureCollection", features };
  }

  function updateAnnotations() {
    if (!port || !port.hasSource(LAYER.ANNOTATIONS)) return;
    port.setSourceData(
      LAYER.ANNOTATIONS,
      snapOverlay(
        inputs.annotations(),
        inputs.engine(),
        (a) => a.timestamp,
        (a) => ({
          id: a.id,
          type: a.type,
          timestamp: a.timestamp,
          notes: a.notes,
          isPending: a.id < 0,
        }),
      ),
    );
  }

  function updateModes() {
    if (!port || !port.hasSource(LAYER.MODES)) return;
    port.setSourceData(
      LAYER.MODES,
      snapOverlay(
        inputs.modes(),
        inputs.engine(),
        (m) => m.started_at,
        (m) => ({
          id: m.id,
          mode: m.mode,
          started_at: m.started_at,
          ended_at: m.ended_at,
          notes: m.notes,
          isPending: m.id < 0,
        }),
      ),
    );
  }

  /**
   * The crosshair marks the cursor on views that have no other sign of it. Where
   * a deck owns time the boat marker already stands on the playhead, so the
   * crosshair is a second marker for the same moment sitting underneath the
   * first — the playhead getter is the signal to leave it off, the same signal
   * that suppresses the points-reveal animation.
   */
  // Starts true so the first call clears the layer once; after that an already
  // empty cursor is never rewritten. The cursor moves every frame during replay,
  // and writing the same empty collection sixty times a second is pure cost.
  let cursorHasFeature = true;

  function updateCursor() {
    if (!port || !port.hasSource(LAYER.CURSOR)) return;
    const pos = inputs.playhead ? null : inputs.cursor();
    if (pos) {
      port.setSourceData(LAYER.CURSOR, {
        type: "Feature",
        geometry: { type: "Point", coordinates: pos },
        properties: {},
      });
      cursorHasFeature = true;
      return;
    }
    if (!cursorHasFeature) return;
    port.setSourceData(LAYER.CURSOR, EMPTY_FC);
    cursorHasFeature = false;
  }

  /**
   * The boat marker. Direction comes from the fix's course (heading if the
   * course is missing) and the marker turns the hull — the dots themselves stay
   * round, because a tip at real dot spacing merges into the next dot.
   *
   * The hull wears the speed colour of the fix it stands on, so the boat reads
   * as part of the track rather than an object on top of it. The band is
   * resolved here rather than in a style expression, which keeps the travel
   * mode's thresholds in one place — the same ones the dots interpolate.
   *
   * Replay advances the playhead once a frame but fixes are a second apart, so
   * at 1× the same fix is asked for eighty times in a row; `lastBoatKey` is what
   * turns those eighty into one.
   */
  let lastBoatKey: string | null = null;

  function updateBoat() {
    if (!port) return;

    const engine = inputs.engine();
    const at = inputs.playhead?.() ?? null;
    const snapped = engine === null || at === null ? null : engine.snapToPoint(at);

    if (!snapped) {
      if (lastBoatKey === null) return;
      lastBoatKey = null;
      port.boat.hide();
      return;
    }

    const props = snapped.feature.properties ?? {};
    const bearing = props.course ?? props.heading ?? 0;
    const color = SPEED_COLORS[speedBucket(props.speed, inputs.travelMode())];
    const centre = snapped.feature.geometry.coordinates as [number, number];

    const key = `${centre.join()}|${bearing}|${color}`;
    if (key === lastBoatKey) return;
    lastBoatKey = key;

    port.boat.show(centre, bearing, color);
  }

  /**
   * Replay holds whatever zoom you are on and pans to keep the boat centred —
   * it never refits, so playback cannot yank the camera out from under you.
   * Zooming mid-replay therefore sets the zoom replay keeps. Selecting still
   * fits, because that is a deliberate act rather than a consequence of time
   * passing.
   *
   * The camera moves when the boat does and not otherwise, for the same reason
   * the source is only rewritten when the fix changes: a camera move per frame
   * is a camera move per frame whether or not the centre differs, and it is the
   * half of the pair that runs ahead of the other.
   */
  let lastCentre: string | null = null;
  let wasPlaying = false;

  function followBoat() {
    const playing = inputs.isPlaying?.() ?? false;
    if (!port || !playing) {
      wasPlaying = playing;
      return;
    }
    // Pressing Play re-centres even if the boat has not moved since: the view
    // may have been panned away in the meantime.
    if (!wasPlaying) lastCentre = null;
    wasPlaying = true;

    const engine = inputs.engine();
    const at = inputs.playhead?.() ?? null;
    if (engine === null || at === null) return;
    const snapped = engine.snapToPoint(at);
    if (!snapped) return;
    const centre = snapped.feature.geometry.coordinates as [number, number];
    const key = centre.join();
    if (key === lastCentre) return;
    lastCentre = key;
    port.easeTo(centre, 0);
  }

  function updateColorScale() {
    if (!port) return;
    if (port.hasLayer(LAYER.POINTS)) {
      port.setPaintProperty(LAYER.POINTS, "circle-color", buildColorScale(inputs.travelMode()));
    }
  }

  function setAnimationEnabled(on: boolean) {
    if (on === animationEnabled) return;
    animationEnabled = on;
    notifyAnimationChange();
    // The control exists from `attach`, but nothing is prepared to animate until
    // the source has loaded. Toggling before then only sets the flag; bootstrap
    // honours it when it runs.
    if (!sourceInitialized) return;
    animatePoints();
  }

  function bindInteractions(p: MapPort) {
    subscriptions.push(
      p.onLayerClick(LAYER.ANNOTATIONS, (e) => {
        const props = e.feature.properties ?? {};
        p.popup.show(e.lngLat, renderAnnotationPopup(props));
      }),
      p.onLayerHover(
        LAYER.ANNOTATIONS,
        () => p.setCursor("pointer"),
        () => p.setCursor(""),
      ),
      p.onLayerClick(LAYER.MODES, (e) => {
        const props = e.feature.properties ?? {};
        p.popup.show(e.lngLat, renderModePopup(props));
      }),
      p.onLayerHover(
        LAYER.MODES,
        () => p.setCursor("pointer"),
        () => p.setCursor(""),
      ),
      p.onMapClick((e) => {
        if (inputs.cursor() != null) {
          inputs.emitMapClick(e.lngLat);
        }
      }),
    );
  }

  function bootstrap() {
    preparePoints();
    animatePoints();
    zoomToMarkers();
    updateAnnotations();
    updateModes();
    updateCursor();
    updateColorScale();
    updateScopeOpacity();
    updateBoat();
  }

  function attach(p: MapPort) {
    port = p;
    sourceInitialized = false;

    p.controls.addFullscreen();
    p.controls.addScale("nautical");
    // The deck's transport owns time when there is a playhead; the reveal
    // animation would be a second, competing way to move through the track.
    if (!inputs.playhead) {
      p.controls.addAnimationControl(buildAnimationControlModel());
    }

    bindInteractions(p);

    subscriptions.push(
      p.onSourceData((e) => {
        if (e.sourceId === LAYER.POINTS && e.isSourceLoaded && !sourceInitialized) {
          sourceInitialized = true;
          bootstrap();
        }
      }),
    );
  }

  function dispose() {
    stopRaf();
    // The marker is a DOM element the adapter owns, not a source the map clears
    // when its style goes — so it has to be taken off explicitly.
    port?.boat.hide();
    for (const unsub of subscriptions) unsub();
    subscriptions.length = 0;
    animationListeners.clear();
    port = null;
    sourceInitialized = false;
    animationEnabled = false;
    progress = 0;
    lastRenderedProgress = -1;
    lastBoatKey = null;
    lastCentre = null;
    wasPlaying = false;
  }

  return {
    attach,
    onMarkersChanged() {
      if (!sourceInitialized) return;
      // A different track means the fix the boat stands on is a different fix,
      // whatever its coordinates say.
      lastBoatKey = null;
      lastCentre = null;
      preparePoints();
      animatePoints();
      zoomToMarkers();
      updateAnnotations();
      updateModes();
    },
    onTravelModeChanged() {
      if (!sourceInitialized) return;
      updateColorScale();
      // The hull's colour band comes off the same thresholds as the dots.
      updateBoat();
    },
    onCursorChanged() {
      if (!sourceInitialized) return;
      updateCursor();
    },
    onPlayheadChanged() {
      if (!sourceInitialized) return;
      updateBoat();
      followBoat();
    },
    onAnnotationsChanged() {
      if (!sourceInitialized) return;
      updateAnnotations();
    },
    onModesChanged() {
      if (!sourceInitialized) return;
      updateModes();
    },
    onScopeChanged() {
      if (!sourceInitialized) return;
      updateScopeOpacity();
      zoomToMarkers();
    },
    onScopeWindowChanged() {
      if (!sourceInitialized) return;
      updateScopeOpacity();
    },
    setAnimationEnabled,
    isAnimationEnabled: () => animationEnabled,
    getAnimationSpeed: () => animationSpeed,
    dispose,
  };
}
