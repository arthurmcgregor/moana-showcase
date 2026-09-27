import { describe, it, expect } from "vitest";
import { createTimelineEngine } from "../timelineEngine";
import type { TimelineData } from "../types";
import type { Mode, Annotation, GeoJSONFeature, GeoJSONFeatureCollection } from "@/api/web";

// ── Helpers ──────────────────────────────────────────────────────────────────

function makeFeature(timestamp: string, lng: number, lat: number, speed = 5): GeoJSONFeature {
  return {
    type: "Feature",
    geometry: { type: "Point", coordinates: [lng, lat] },
    properties: { timestamp, speed, track_id: 1 },
  };
}

function makeMode(id: number, mode: string, started_at: string, ended_at?: string): Mode {
  return {
    id,
    vessel: 1,
    mode: mode as Mode["mode"],
    started_at,
    ended_at,
    notes: "",
    metadata: {},
  };
}

function makeAnnotation(id: number, type: string, timestamp: string): Annotation {
  return {
    id,
    vessel: 1,
    type: type as Annotation["type"],
    timestamp,
    notes: "",
    metadata: {},
  };
}

function makeCollection(features: GeoJSONFeature[]): GeoJSONFeatureCollection {
  return { type: "FeatureCollection", features };
}

const T0 = "2024-04-01T08:00:00Z"; // journey start
const T1 = "2024-04-01T09:00:00Z"; // 1h in
const T2 = "2024-04-01T10:00:00Z"; // 2h in
const T3 = "2024-04-01T11:00:00Z"; // 3h in
const T4 = "2024-04-01T12:00:00Z"; // journey end

const T0_MS = new Date(T0).getTime();
const T1_MS = new Date(T1).getTime();
const T2_MS = new Date(T2).getTime();
const T3_MS = new Date(T3).getTime();
const T4_MS = new Date(T4).getTime();

function makeBasicData(overrides?: Partial<TimelineData>): TimelineData {
  return {
    features: makeCollection([
      makeFeature(T0, 174.0, -36.0),
      makeFeature(T1, 174.1, -36.1),
      makeFeature(T2, 174.2, -36.2),
      makeFeature(T3, 174.3, -36.3),
      makeFeature(T4, 174.4, -36.4),
    ]),
    modes: [
      makeMode(1, "SAIL", T0, T2),
      makeMode(2, "MOTOR", T2, T4),
    ],
    annotations: [
      makeAnnotation(1, "TACK", T1),
      makeAnnotation(2, "GYBE", T3),
    ],
    journeyStart: T0,
    journeyEnd: T4,
    ...overrides,
  };
}

// ── Tests ────────────────────────────────────────────────────────────────────

describe("createTimelineEngine", () => {
  describe("timeToPercentage / percentageToTime", () => {
    it("converts start to 0% and end to 100%", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.timeToPercentage(T0_MS)).toBe(0);
      expect(engine.timeToPercentage(T4_MS)).toBe(100);
    });

    it("converts midpoint to 50%", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.timeToPercentage(T2_MS)).toBe(50);
    });

    it("clamps values outside range", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.timeToPercentage(T0_MS - 100000)).toBe(0);
      expect(engine.timeToPercentage(T4_MS + 100000)).toBe(100);
    });

    it("round-trips through percentageToTime", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.percentageToTime(0)).toBe(T0_MS);
      expect(engine.percentageToTime(100)).toBe(T4_MS);
      expect(engine.percentageToTime(50)).toBe(T2_MS);
    });
  });

  describe("snapToPoint", () => {
    it("snaps to the nearest feature by timestamp", () => {
      const engine = createTimelineEngine(makeBasicData());
      // Slightly after T1
      const result = engine.snapToPoint(T1_MS + 1000);
      expect(result).not.toBeNull();
      expect(result!.index).toBe(1); // T1 feature
    });

    it("snaps to first point for time before start", () => {
      const engine = createTimelineEngine(makeBasicData());
      const result = engine.snapToPoint(T0_MS - 100000);
      expect(result!.index).toBe(0);
    });

    it("snaps to last point for time after end", () => {
      const engine = createTimelineEngine(makeBasicData());
      const result = engine.snapToPoint(T4_MS + 100000);
      expect(result!.index).toBe(4);
    });

    it("returns null for empty features", () => {
      const engine = createTimelineEngine(
        makeBasicData({ features: makeCollection([]) }),
      );
      expect(engine.snapToPoint(T1_MS)).toBeNull();
    });
  });

  describe("snapToPointAtOrAfter", () => {
    it("snaps forward, not to the nearer sample behind", () => {
      // The real shape this guards: mode boundaries carry sub-second precision
      // while GPS samples land on whole seconds, so the sample just *before* a
      // segment's start is routinely the nearest one to it.
      const engine = createTimelineEngine(
        makeBasicData({
          features: makeCollection([
            makeFeature("2024-04-01T08:00:54Z", 174.0, -36.0),
            makeFeature("2024-04-01T08:00:56Z", 174.1, -36.1),
            makeFeature("2024-04-01T08:00:57Z", 174.2, -36.2),
          ]),
        }),
      );
      const boundary = new Date("2024-04-01T08:00:56.136Z").getTime();

      // Nearest is 136ms behind the boundary; forward is 864ms ahead of it.
      expect(engine.snapToPoint(boundary)!.index).toBe(1);
      expect(engine.snapToPointAtOrAfter(boundary)!.index).toBe(2);
    });

    it("stays put on an exact sample", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.snapToPointAtOrAfter(T1_MS)!.index).toBe(1);
    });

    it("gives the first point for a time before the start", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.snapToPointAtOrAfter(T0_MS - 100000)!.index).toBe(0);
    });

    it("has nothing beyond the last point", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.snapToPointAtOrAfter(T4_MS + 100000)).toBeNull();
    });

    it("returns null for empty features", () => {
      const engine = createTimelineEngine(makeBasicData({ features: makeCollection([]) }));
      expect(engine.snapToPointAtOrAfter(T1_MS)).toBeNull();
    });
  });

  describe("modeAtTime", () => {
    it("returns the active mode", () => {
      const engine = createTimelineEngine(makeBasicData());
      const mode = engine.modeAtTime(T1_MS);
      expect(mode).not.toBeNull();
      expect(mode!.mode).toBe("SAIL");
    });

    it("returns MOTOR mode in second half", () => {
      const engine = createTimelineEngine(makeBasicData());
      const mode = engine.modeAtTime(T3_MS);
      expect(mode).not.toBeNull();
      expect(mode!.mode).toBe("MOTOR");
    });

    it("returns null when no modes exist", () => {
      const engine = createTimelineEngine(makeBasicData({ modes: [] }));
      expect(engine.modeAtTime(T1_MS)).toBeNull();
    });

    it("handles mode with no ended_at (open-ended)", () => {
      const engine = createTimelineEngine(
        makeBasicData({
          modes: [makeMode(1, "ANCHOR", T0)], // no ended_at
        }),
      );
      expect(engine.modeAtTime(T4_MS)?.mode).toBe("ANCHOR");
    });
  });

  describe("modeBefore", () => {
    it("returns the mode preceding the active mode", () => {
      const engine = createTimelineEngine(makeBasicData());
      // T3 is in MOTOR (started T2); preceding is SAIL (started T0)
      expect(engine.modeBefore(T3_MS)?.mode).toBe("SAIL");
    });

    it("returns null when active mode is the first", () => {
      const engine = createTimelineEngine(makeBasicData());
      // T1 is in SAIL (first mode)
      expect(engine.modeBefore(T1_MS)).toBeNull();
    });

    it("returns null when no mode is active", () => {
      const engine = createTimelineEngine(makeBasicData({ modes: [] }));
      expect(engine.modeBefore(T1_MS)).toBeNull();
    });
  });

  describe("modeColorAtTime", () => {
    it("returns the mode color for active mode", () => {
      const engine = createTimelineEngine(makeBasicData());
      // SAIL color is #0d6efd
      expect(engine.modeColorAtTime(T1_MS)).toBe("#0d6efd");
    });

    it("returns default color when no mode is active", () => {
      const engine = createTimelineEngine(makeBasicData({ modes: [] }));
      expect(engine.modeColorAtTime(T1_MS)).toBe("#6c757d");
    });
  });

  describe("isNearModeStart", () => {
    it("returns true when cursor is at the same GPS point as mode start", () => {
      const engine = createTimelineEngine(makeBasicData());
      // T0 is exactly a feature timestamp and the start of SAIL mode
      expect(engine.isNearModeStart(T0_MS)).toBe(true);
    });

    it("returns true when cursor snaps to same point as mode start", () => {
      const engine = createTimelineEngine(makeBasicData());
      // Slightly offset from T2 (MOTOR start) should snap to same point
      expect(engine.isNearModeStart(T2_MS + 100)).toBe(true);
    });

    it("returns false when cursor is far from mode start", () => {
      const engine = createTimelineEngine(makeBasicData());
      // T1 is well within SAIL mode but not at its start
      expect(engine.isNearModeStart(T1_MS)).toBe(false);
    });

    it("returns false when no mode is active", () => {
      const engine = createTimelineEngine(makeBasicData({ modes: [] }));
      expect(engine.isNearModeStart(T1_MS)).toBe(false);
    });
  });

  describe("annotationsNear", () => {
    it("returns annotations within default 2-minute window", () => {
      const engine = createTimelineEngine(makeBasicData());
      const near = engine.annotationsNear(T1_MS);
      expect(near).toHaveLength(1);
      expect(near[0].type).toBe("TACK");
    });

    it("returns empty when no annotations are near", () => {
      const engine = createTimelineEngine(makeBasicData());
      // Midpoint between T1 and T3 — both annotations are >30min away
      const mid = (T1_MS + T3_MS) / 2;
      expect(engine.annotationsNear(mid)).toHaveLength(0);
    });

    it("supports custom window size", () => {
      const engine = createTimelineEngine(makeBasicData());
      // With a 2-hour window, both annotations should be found from midpoint
      const mid = (T1_MS + T3_MS) / 2;
      const near = engine.annotationsNear(mid, 2 * 60 * 60 * 1000);
      expect(near).toHaveLength(2);
    });

    it("returns empty when no annotations exist", () => {
      const engine = createTimelineEngine(makeBasicData({ annotations: [] }));
      expect(engine.annotationsNear(T1_MS)).toHaveLength(0);
    });
  });

  describe("nearestPointToCoordinate", () => {
    it("finds the nearest feature by coordinate", () => {
      const engine = createTimelineEngine(makeBasicData());
      // Close to T2 feature at [174.2, -36.2]
      const result = engine.nearestPointToCoordinate([174.19, -36.19]);
      expect(result).not.toBeNull();
      expect(result!.index).toBe(2);
    });

    it("returns null for empty features", () => {
      const engine = createTimelineEngine(
        makeBasicData({ features: makeCollection([]) }),
      );
      expect(engine.nearestPointToCoordinate([174.0, -36.0])).toBeNull();
    });
  });

  describe("modeGradientCSS", () => {
    it("returns a linear-gradient string with mode colors", () => {
      const engine = createTimelineEngine(makeBasicData());
      const gradient = engine.modeGradientCSS();
      expect(gradient).toMatch(/^linear-gradient\(to right,/);
      // Should contain SAIL color (#0d6efd) with alpha
      expect(gradient).toContain("#0d6efd80");
      // Should contain MOTOR color (#dc3545) with alpha
      expect(gradient).toContain("#dc354580");
    });

    it("returns plain color when no modes", () => {
      const engine = createTimelineEngine(makeBasicData({ modes: [] }));
      expect(engine.modeGradientCSS()).toBe("#f0f1f3");
    });
  });

  describe("modeColorsForFeatures", () => {
    it("returns one color per feature", () => {
      const data = makeBasicData();
      const engine = createTimelineEngine(data);
      const colors = engine.modeColorsForFeatures(data.features.features);
      expect(colors).toHaveLength(5);
    });

    it("assigns SAIL color to features in SAIL mode range", () => {
      const data = makeBasicData();
      const engine = createTimelineEngine(data);
      const colors = engine.modeColorsForFeatures(data.features.features);
      // T0 and T1 are in SAIL mode
      expect(colors[0]).toBe("#0d6efd");
      expect(colors[1]).toBe("#0d6efd");
    });

    it("assigns MOTOR color to features in MOTOR mode range", () => {
      const data = makeBasicData();
      const engine = createTimelineEngine(data);
      const colors = engine.modeColorsForFeatures(data.features.features);
      // T3 and T4 are in MOTOR mode
      expect(colors[3]).toBe("#dc3545");
      expect(colors[4]).toBe("#dc3545");
    });

    it("returns default colors when no modes", () => {
      const data = makeBasicData({ modes: [] });
      const engine = createTimelineEngine(data);
      const colors = engine.modeColorsForFeatures(data.features.features);
      expect(colors.every((c) => c === "#6c757d")).toBe(true);
    });
  });

  describe("metadata", () => {
    it("exposes startMs and endMs", () => {
      const engine = createTimelineEngine(makeBasicData());
      expect(engine.startMs).toBe(T0_MS);
      expect(engine.endMs).toBe(T4_MS);
    });

    it("uses last feature time when journeyEnd is null", () => {
      const engine = createTimelineEngine(
        makeBasicData({ journeyEnd: null }),
      );
      expect(engine.endMs).toBe(T4_MS); // last feature is at T4
    });
  });

  describe("edge cases", () => {
    it("handles single feature", () => {
      const data = makeBasicData({
        features: makeCollection([makeFeature(T0, 174.0, -36.0)]),
        modes: [],
        annotations: [],
        journeyEnd: T0,
      });
      const engine = createTimelineEngine(data);
      expect(engine.timeToPercentage(T0_MS)).toBe(0);
      expect(engine.snapToPoint(T0_MS)!.index).toBe(0);
    });

    it("handles zero-duration journey (start === end)", () => {
      const data = makeBasicData({
        features: makeCollection([makeFeature(T0, 174.0, -36.0)]),
        journeyStart: T0,
        journeyEnd: T0,
        modes: [],
      });
      const engine = createTimelineEngine(data);
      expect(engine.timeToPercentage(T0_MS)).toBe(0);
      expect(engine.percentageToTime(50)).toBe(T0_MS);
    });

    it("handles empty features with modes", () => {
      const data = makeBasicData({
        features: makeCollection([]),
      });
      const engine = createTimelineEngine(data);
      expect(engine.snapToPoint(T1_MS)).toBeNull();
      expect(engine.modeAtTime(T1_MS)?.mode).toBe("SAIL");
    });
  });
});
