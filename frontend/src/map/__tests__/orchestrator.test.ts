import { describe, it, expect } from "vitest";
import type { FeatureCollection, Feature } from "geojson";
import {
  createMapOrchestrator,
  buildColorScale,
  FULL_OPACITY,
  buildScopeOpacity,
  LAYER,
} from "../orchestrator";
import { SPEED_COLORS } from "../speedScale";
import { createFakeMapPort } from "../__fakes__/FakeMapPort";
import type { Annotation, GeoJSONFeature, Mode } from "@/api/web";
import type { TimelineEngine, SnappedPoint } from "@/engine";

const at = (s: number) => new Date(Date.UTC(2024, 0, 1, 0, 0, s)).getTime();

function featureAt(i: number): GeoJSONFeature {
  return {
    type: "Feature",
    geometry: { type: "Point", coordinates: [i * 0.001, -43.5] },
    properties: {
      timestamp: new Date(Date.UTC(2024, 0, 1, 0, 0, i)).toISOString(),
      speed: 5,
      track_id: 1,
    },
  };
}

function markersOf(n: number): FeatureCollection {
  return { type: "FeatureCollection", features: Array.from({ length: n }, (_, i) => featureAt(i)) };
}

// A minimal TimelineEngine that snaps every request to the first feature.
function fakeEngine(features: GeoJSONFeature[]): TimelineEngine {
  const snap: SnappedPoint | null = features.length
    ? { index: 0, feature: features[0] }
    : null;
  return {
    timeToPercentage: () => 0,
    percentageToTime: () => 0,
    snapToPoint: () => snap,
    modeAtTime: () => null,
    modeColorAtTime: () => "#000",
    isNearModeStart: () => false,
    annotationsNear: () => [],
    nearestPointToCoordinate: () => snap,
    modeGradientCSS: () => "",
    modeColorsForFeatures: (fs) => fs.map(() => "#000"),
    startMs: 0,
    endMs: 0,
  };
}

interface Harness {
  markers: FeatureCollection;
  travelMode: string | undefined;
  cursor: [number, number] | null | undefined;
  annotations: Annotation[];
  modes: Mode[];
  rafCbs: Array<() => void>;
  emitted: Array<[number, number]>;
  scopeWindow: { startMs: number; endMs: number } | null;
  playheadMs: number | null;
  hasPlayhead: boolean;
  isPlaying: boolean;
}

function makeOrchestrator(overrides: Partial<Harness> = {}) {
  const state: Harness = {
    markers: markersOf(100),
    travelMode: "sailing",
    cursor: null,
    annotations: [],
    modes: [],
    rafCbs: [],
    emitted: [],
    scopeWindow: null,
    playheadMs: null,
    hasPlayhead: false,
    isPlaying: false,
    ...overrides,
  };
  const o = createMapOrchestrator({
    markers: () => state.markers,
    travelMode: () => state.travelMode,
    cursor: () => state.cursor,
    annotations: () => state.annotations,
    modes: () => state.modes,
    engine: () => fakeEngine(state.markers.features as GeoJSONFeature[]),
    emitMapClick: (xy) => state.emitted.push(xy),
    raf: (cb) => {
      state.rafCbs.push(cb);
      return state.rafCbs.length;
    },
    cancelRaf: () => {},
    scopeWindow: () => state.scopeWindow,
    ...(state.hasPlayhead ? { playhead: () => state.playheadMs } : {}),
    isPlaying: () => state.isPlaying,
  });
  return { o, state };
}

describe("MapOrchestrator", () => {
  describe("attach + init-order race", () => {
    it("does not touch the map before sourcedata fires for 'points'", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);

      expect(fake.sources.size).toBe(0);
      expect(fake.fits).toHaveLength(0);
      expect(state.rafCbs).toHaveLength(0);

      // Fire sourcedata for a different source — should not bootstrap.
      fake.emitSourceData({ sourceId: "other", isSourceLoaded: true });
      expect(fake.fits).toHaveLength(0);

      // Fire 'points' but not yet loaded — should not bootstrap.
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: false });
      expect(fake.fits).toHaveLength(0);
    });

    it("bootstraps exactly once when 'points' becomes loaded", () => {
      const { o } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);

      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      expect(fake.fits).toHaveLength(1);
    });

    // The animation control is installed by `attach`, so it can be toggled in
    // the gap before the source loads — when there is nothing prepared to
    // animate. Bootstrap is what honours the flag.
    it("holds an early animation toggle until the source is ready", () => {
      const { o, state } = makeOrchestrator({ markers: markersOf(3) });
      const fake = createFakeMapPort();
      o.attach(fake.port);

      o.setAnimationEnabled(true);
      expect(o.isAnimationEnabled()).toBe(true);
      expect(fake.sources.size).toBe(0);
      expect(state.rafCbs).toHaveLength(0);

      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });
      expect(state.rafCbs.length).toBeGreaterThan(0);
    });

    it("installs controls in order: fullscreen, scale, animation", () => {
      const { o } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      expect(fake.addedControls).toEqual(["fullscreen", "scale", "animation"]);
    });
  });

  describe("animation", () => {
    it("when disabled, writes full feature set to 'points' on bootstrap", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      const fc = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(fc.features).toHaveLength(100);
      expect(state.rafCbs).toHaveLength(0);
    });

    it("slices features up to progress on each rAF step when enabled", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      // Toggle animation via the control model. animatePoints runs one step
      // synchronously (progress += speed → 10) and then schedules the next.
      fake.animationControl!.toggle();
      expect(state.rafCbs.length).toBe(1);
      let fc = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(fc.features).toHaveLength(10);

      state.rafCbs.pop()!(); // progress = 20
      fc = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(fc.features).toHaveLength(20);

      state.rafCbs.pop()!(); // progress = 30
      fc = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(fc.features).toHaveLength(30);
    });

    it("wraps progress back to 0 once it exceeds total", () => {
      const { o, state } = makeOrchestrator({ markers: markersOf(15) });
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      fake.animationControl!.toggle(); // synchronous step → progress 10
      state.rafCbs.pop()!();           // progress 20 → wraps to 0
      const fc = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(fc.features).toHaveLength(0);
    });

    it("cycleSpeed advances through speed steps", () => {
      const { o } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      const ctrl = fake.animationControl!;
      expect(ctrl.getSpeed()).toBe(10);
      ctrl.cycleSpeed();
      expect(ctrl.getSpeed()).toBe(25);
      ctrl.cycleSpeed();
      expect(ctrl.getSpeed()).toBe(50);
    });

    it("notifies onStateChange listeners on toggle and cycleSpeed", () => {
      const { o } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      const ctrl = fake.animationControl!;
      let n = 0;
      ctrl.onStateChange(() => n++);
      ctrl.toggle();
      ctrl.cycleSpeed();
      expect(n).toBe(2);
    });
  });

  describe("fitBounds", () => {
    it("collects coordinates from all features and fits once", () => {
      const { o } = makeOrchestrator({ markers: markersOf(3) });
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      expect(fake.fits).toHaveLength(1);
      expect(fake.fits[0].coords).toHaveLength(3);
      expect(fake.fits[0].padding).toBe(50);
      expect(fake.fits[0].durationMs).toBe(0);
    });

    // The points source is the fixes alone, but a line's coordinates still bound
    // the camera — which is why the prepared features and the prepared points
    // are two lists rather than one.
    it("counts a line towards the bounds without putting it in the source", () => {
      const markers = markersOf(3);
      markers.features.push({
        type: "Feature",
        geometry: { type: "LineString", coordinates: [[1, -43.5], [2, -43.5]] },
        properties: { timestamp: new Date(Date.UTC(2024, 0, 1, 0, 0, 1)).toISOString() },
      });
      const { o } = makeOrchestrator({ markers });
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      expect(fake.fits[0].coords).toHaveLength(5);
      const source = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(source.features).toHaveLength(3);
    });

    it("skips fitBounds when markers are empty", () => {
      const { o } = makeOrchestrator({ markers: markersOf(0) });
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });
      expect(fake.fits).toHaveLength(0);
    });
  });

  describe("travelMode", () => {
    it("sets the initial colour scale on bootstrap", () => {
      const { o, state } = makeOrchestrator();
      state.travelMode = "driving";
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      const paint = fake.paint[LAYER.POINTS]?.["circle-color"] as unknown[];
      expect(paint).toBeDefined();
      // driving thresholds include 54 — walking/sailing/biking do not.
      expect(JSON.stringify(paint)).toContain("54");
    });

    it("updates the colour scale when travelMode changes after bootstrap", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      state.travelMode = "walking";
      o.onTravelModeChanged();
      const paint = fake.paint[LAYER.POINTS]?.["circle-color"] as unknown[];
      expect(JSON.stringify(paint)).toEqual(JSON.stringify(buildColorScale("walking")));
    });

    it("does not push paint before bootstrap", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      state.travelMode = "driving";
      o.onTravelModeChanged();
      expect(fake.paint[LAYER.POINTS]).toBeUndefined();
    });
  });

  describe("cursor", () => {
    it("writes a single-feature collection when cursor is set, empty when null", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      state.cursor = [172.5, -43.5];
      o.onCursorChanged();
      const feat = fake.sources.get(LAYER.CURSOR) as Feature;
      expect(feat.geometry.type).toBe("Point");

      state.cursor = null;
      o.onCursorChanged();
      const fc = fake.sources.get(LAYER.CURSOR) as FeatureCollection;
      expect(fc.features).toEqual([]);
    });

    it("emits mapClick only when cursor is set", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);

      fake.emitMapClick([1, 2]);
      expect(state.emitted).toEqual([]);

      state.cursor = [172.5, -43.5];
      fake.emitMapClick([3, 4]);
      expect(state.emitted).toEqual([[3, 4]]);
    });
  });

  describe("overlays", () => {
    it("snaps annotations via the timeline engine and writes to 'annotations' source", () => {
      const { o, state } = makeOrchestrator();
      state.annotations = [
        {
          id: 1,
          vessel: 1,
          type: "TACK",
          timestamp: "2024-01-01T00:00:05Z",
          metadata: {},
          notes: "",
        },
      ];
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      const fc = fake.sources.get(LAYER.ANNOTATIONS) as FeatureCollection;
      expect(fc.features).toHaveLength(1);
      expect(fc.features[0].properties).toMatchObject({ id: 1, type: "TACK" });
    });

    it("snaps modes and writes to 'modes' source", () => {
      const { o, state } = makeOrchestrator();
      state.modes = [
        {
          id: 7,
          vessel: 1,
          mode: "SAIL",
          started_at: "2024-01-01T00:00:03Z",
          metadata: {},
          notes: "",
        },
      ];
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      const fc = fake.sources.get(LAYER.MODES) as FeatureCollection;
      expect(fc.features).toHaveLength(1);
      expect(fc.features[0].properties).toMatchObject({ id: 7, mode: "SAIL" });
    });

    it("shows a popup when an annotation layer feature is clicked", () => {
      const { o } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      fake.emitLayerClick(LAYER.ANNOTATIONS, {
        feature: {
          type: "Feature",
          geometry: { type: "Point", coordinates: [1, 2] },
          properties: { type: "TACK", timestamp: "2024-01-01T00:00:00Z", notes: "" },
        },
        lngLat: [1, 2],
      });

      expect(fake.lastPopup).not.toBeNull();
      expect(fake.lastPopup!.html).toContain("TACK");
    });
  });

  describe("dispose", () => {
    it("removes all listeners and stops responding to events", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      expect(fake.listenerCount).toBeGreaterThan(0);
      o.dispose();
      expect(fake.listenerCount).toBe(0);

      // Post-dispose events are inert.
      state.cursor = [1, 2];
      fake.emitMapClick([9, 9]);
      expect(state.emitted).toEqual([]);
    });

    it("onMarkersChanged is a no-op before bootstrap", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      state.markers = markersOf(5);
      o.onMarkersChanged();
      expect(fake.sources.has(LAYER.POINTS)).toBe(false);
    });
  });

  describe("onMarkersChanged after bootstrap", () => {
    it("re-animates, re-fits, and re-snaps overlays", () => {
      const { o, state } = makeOrchestrator();
      const fake = createFakeMapPort();
      o.attach(fake.port);
      fake.emitSourceData({ sourceId: "points", isSourceLoaded: true });

      const fitsBefore = fake.fits.length;
      state.markers = markersOf(5);
      o.onMarkersChanged();

      expect(fake.fits.length).toBe(fitsBefore + 1);
      const fc = fake.sources.get(LAYER.POINTS) as FeatureCollection;
      expect(fc.features).toHaveLength(5);
    });
  });
});

describe("MapOrchestrator — scope and dimming", () => {
  const scopeOf = (from: number, to: number) => ({
    startMs: new Date(Date.UTC(2024, 0, 1, 0, 0, from)).getTime(),
    endMs: new Date(Date.UTC(2024, 0, 1, 0, 0, to)).getTime(),
  });

  function pointsIn(fake: ReturnType<typeof createFakeMapPort>): Feature[] {
    const data = fake.sources.get(LAYER.POINTS) as FeatureCollection;
    return data.features;
  }

  it("leaves every fix undimmed while nothing is selected", () => {
    const { o } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });

    expect(fake.paint[LAYER.POINTS]["circle-opacity"]).toBe(FULL_OPACITY);
  });

  it("stamps each fix with its time so dimming needs no second source", () => {
    const { o } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });

    const stamps = pointsIn(fake).map((f) => f.properties?.tMs);
    expect(stamps).toEqual(Array.from({ length: 10 }, (_, i) => at(i)));
  });

  it("drops everything outside the selection to one flat floor", () => {
    const { o, state } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });

    state.scopeWindow = scopeOf(3, 6);
    o.onScopeChanged();

    expect(fake.paint[LAYER.POINTS]["circle-opacity"]).toEqual(
      buildScopeOpacity(scopeOf(3, 6)),
    );
  });

  it("changes the window without rewriting the track", () => {
    const { o, state } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });
    const before = pointsIn(fake);

    state.scopeWindow = scopeOf(3, 6);
    o.onScopeWindowChanged();

    // The same feature objects: a scope change is a paint property, nothing more.
    expect(pointsIn(fake)).toBe(before);
  });

  it("returns to nothing dimmed when the selection is cleared", () => {
    const { o, state } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });

    state.scopeWindow = scopeOf(3, 6);
    o.onScopeChanged();
    state.scopeWindow = null;
    o.onScopeChanged();

    expect(fake.paint[LAYER.POINTS]["circle-opacity"]).toBe(FULL_OPACITY);
  });

  it("fits the camera to the selection alone", () => {
    const { o, state } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });

    const whole = fake.fits.at(-1)!;
    expect(whole.coords).toHaveLength(10);

    state.scopeWindow = scopeOf(3, 6);
    o.onScopeChanged();

    const scoped = fake.fits.at(-1)!;
    expect(scoped.coords).toHaveLength(4);
    expect(scoped.coords[0]).toEqual([0.003, -43.5]);
  });

  it("keeps the camera where it is when a selection holds no fixes", () => {
    const { o, state } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });
    const fitsBefore = fake.fits.length;

    state.scopeWindow = { startMs: 10, endMs: 20 };
    o.onScopeChanged();

    expect(fake.fits).toHaveLength(fitsBefore);
  });

  it("does nothing before the source is initialised", () => {
    const { o, state } = makeOrchestrator({ markers: markersOf(10) });
    const fake = createFakeMapPort();
    o.attach(fake.port);

    state.scopeWindow = scopeOf(3, 6);
    o.onScopeChanged();

    expect(fake.sources.size).toBe(0);
    expect(fake.fits).toHaveLength(0);
  });
});

describe("MapOrchestrator — deck ownership of time", () => {
  it("keeps the reveal animation when no deck owns time", () => {
    const { o } = makeOrchestrator();
    const fake = createFakeMapPort();
    o.attach(fake.port);

    expect(fake.addedControls).toEqual(["fullscreen", "scale", "animation"]);
  });

  it("suppresses the reveal animation once a playhead is supplied", () => {
    const { o } = makeOrchestrator({ hasPlayhead: true });
    const fake = createFakeMapPort();
    o.attach(fake.port);

    expect(fake.addedControls).toEqual(["fullscreen", "scale"]);
    expect(fake.animationControl).toBeNull();
  });

  it("still draws the whole track when the animation control is gone", () => {
    const { o } = makeOrchestrator({ markers: markersOf(10), hasPlayhead: true });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });

    const data = fake.sources.get(LAYER.POINTS) as FeatureCollection;
    expect(data.features).toHaveLength(10);
    expect(o.isAnimationEnabled()).toBe(false);
  });
});

describe("MapOrchestrator — boat and replay camera", () => {
  // The shared fakeEngine snaps everything to feature 0; this one snaps by index
  // so the boat can be seen to move.
  function indexedEngine(features: GeoJSONFeature[], index: () => number): TimelineEngine {
    const base = fakeEngine(features);
    return {
      ...base,
      snapToPoint: () => (features.length ? { index: index(), feature: features[index()] } : null),
    };
  }

  function bootedWithBoat(featureCount = 10) {
    let boatIndex = 0;
    const state = {
      markers: markersOf(featureCount),
      playheadMs: 0 as number | null,
      isPlaying: false,
      scopeWindow: null as { startMs: number; endMs: number } | null,
      travelMode: "sailing" as string | undefined,
      cursor: null as [number, number] | null,
    };
    const o = createMapOrchestrator({
      markers: () => state.markers,
      travelMode: () => state.travelMode,
      cursor: () => state.cursor,
      annotations: () => [],
      modes: () => [],
      engine: () => indexedEngine(state.markers.features as GeoJSONFeature[], () => boatIndex),
      emitMapClick: () => {},
      raf: () => 1,
      cancelRaf: () => {},
      scopeWindow: () => state.scopeWindow,
      playhead: () => state.playheadMs,
      isPlaying: () => state.isPlaying,
    });
    const fake = createFakeMapPort();
    o.attach(fake.port);
    fake.emitSourceData({ sourceId: LAYER.POINTS, isSourceLoaded: true });
    return { o, fake, state, moveBoatTo: (i: number) => { boatIndex = i; } };
  }

  it("places the boat at the playhead with the fix's course as its bearing", () => {
    const { fake } = bootedWithBoat();

    expect(fake.boat?.center).toEqual([0, -43.5]);
    expect(fake.boat?.bearing).toBe(0);
  });

  it("colours the hull for the fix's speed band", () => {
    const { fake } = bootedWithBoat();

    // featureAt() fixes carry 5 kts, which is band 2 of the sailing thresholds.
    expect(fake.boat?.color).toBe(SPEED_COLORS[2]);
  });

  it("rebands the hull when the travel mode changes under it", () => {
    const { o, fake, state } = bootedWithBoat();

    // 5 kts is the top band for walking, where it was mid-scale for sailing.
    state.travelMode = "walking";
    o.onTravelModeChanged();

    expect(fake.boat?.color).toBe(SPEED_COLORS[6]);
  });

  it("repaints the dimming but holds the camera when the window follows the boat", () => {
    const { o, fake, state } = bootedWithBoat();
    const fitsBefore = fake.fits.length;

    const window = { startMs: at(3), endMs: at(6) };
    state.scopeWindow = window;
    o.onScopeWindowChanged();

    // The dimming moved with the window...
    expect(fake.paint[LAYER.POINTS]["circle-opacity"]).toEqual(buildScopeOpacity(window));
    // ...and the camera did not.
    expect(fake.fits.length).toBe(fitsBefore);
  });

  it("leaves the crosshair off where the boat already marks the playhead", () => {
    const { o, fake, state } = bootedWithBoat();

    state.cursor = [0, -43.5];
    o.onCursorChanged();

    expect(fake.sources.get(LAYER.CURSOR)).toEqual({ type: "FeatureCollection", features: [] });
  });

  it("moves the boat as the playhead moves", () => {
    const { o, fake, moveBoatTo } = bootedWithBoat();

    moveBoatTo(4);
    o.onPlayheadChanged();

    expect(fake.boat?.center).toEqual([0.004, -43.5]);
  });

  // Replay ticks once a frame; fixes are a second apart. Rewriting the source on
  // a tick that lands on the same fix hands the map a re-tile it does not need,
  // and the round trip is what leaves the hull a step behind the camera.
  it("moves the boat only when the fix it stands on changes", () => {
    const { o, fake, state, moveBoatTo } = bootedWithBoat();
    const before = fake.boatShows.length;

    state.playheadMs = 1;
    o.onPlayheadChanged();
    state.playheadMs = 2;
    o.onPlayheadChanged();

    expect(fake.boatShows).toHaveLength(before);

    moveBoatTo(4);
    o.onPlayheadChanged();

    expect(fake.boatShows).toHaveLength(before + 1);
  });

  it("moves the camera only when the fix it stands on changes", () => {
    const { o, fake, state, moveBoatTo } = bootedWithBoat();

    state.isPlaying = true;
    moveBoatTo(5);
    o.onPlayheadChanged();
    o.onPlayheadChanged();
    o.onPlayheadChanged();

    expect(fake.eases).toHaveLength(1);
  });

  it("re-centres on a fresh Play even if the boat has not moved", () => {
    const { o, fake, state, moveBoatTo } = bootedWithBoat();

    state.isPlaying = true;
    moveBoatTo(5);
    o.onPlayheadChanged();

    // Paused, panned away, played again on the same fix.
    state.isPlaying = false;
    o.onPlayheadChanged();
    state.isPlaying = true;
    o.onPlayheadChanged();

    expect(fake.eases.map((e) => e.center)).toEqual([[0.005, -43.5], [0.005, -43.5]]);
  });

  it("clears the boat when there is no playhead", () => {
    const { o, fake, state } = bootedWithBoat();

    state.playheadMs = null;
    o.onPlayheadChanged();

    expect(fake.boat).toBeNull();
  });

  it("takes the boat off the map on dispose", () => {
    // The marker is a DOM element, not a source the style clears with it.
    const { o, fake } = bootedWithBoat();
    expect(fake.boat).not.toBeNull();

    o.dispose();

    expect(fake.boat).toBeNull();
  });

  it("leaves the camera alone while paused", () => {
    const { o, fake, moveBoatTo } = bootedWithBoat();
    const fitsBefore = fake.fits.length;

    moveBoatTo(5);
    o.onPlayheadChanged();

    expect(fake.eases).toHaveLength(0);
    expect(fake.fits).toHaveLength(fitsBefore);
  });

  it("pans to the boat while playing, and never refits", () => {
    const { o, fake, state, moveBoatTo } = bootedWithBoat();
    const fitsBefore = fake.fits.length;

    state.isPlaying = true;
    moveBoatTo(5);
    o.onPlayheadChanged();
    moveBoatTo(6);
    o.onPlayheadChanged();

    expect(fake.eases.map((e) => e.center)).toEqual([[0.005, -43.5], [0.006, -43.5]]);
    // Refitting is what would yank a mid-replay zoom away.
    expect(fake.fits).toHaveLength(fitsBefore);
  });

  it("keeps whatever zoom the user set mid-replay", () => {
    const { o, fake, state, moveBoatTo } = bootedWithBoat();

    state.isPlaying = true;
    fake.zoom = 16;
    moveBoatTo(3);
    o.onPlayheadChanged();

    // easeTo carries a centre only, so the zoom the user chose survives.
    expect(fake.zoom).toBe(16);
    expect(fake.eases).toHaveLength(1);
  });

  it("still fits when a selection is made during replay", () => {
    const { o, fake, state } = bootedWithBoat();
    state.isPlaying = true;

    state.scopeWindow = {
      startMs: new Date(Date.UTC(2024, 0, 1, 0, 0, 2)).getTime(),
      endMs: new Date(Date.UTC(2024, 0, 1, 0, 0, 5)).getTime(),
    };
    o.onScopeChanged();

    expect(fake.fits.at(-1)!.coords).toHaveLength(4);
  });
});
