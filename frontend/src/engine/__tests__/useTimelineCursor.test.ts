import { describe, it, expect } from "vitest";
import { ref } from "vue";
import { useTimelineCursor } from "../useTimelineCursor";
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

const T0 = "2024-04-01T08:00:00Z";
const T1 = "2024-04-01T09:00:00Z";
const T2 = "2024-04-01T10:00:00Z";
const T3 = "2024-04-01T11:00:00Z";
const T4 = "2024-04-01T12:00:00Z";

const T0_MS = new Date(T0).getTime();

function makeBasicData(): TimelineData {
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
  };
}

// ── Tests ────────────────────────────────────────────────────────────────────

describe("useTimelineCursor", () => {
  describe("initial state", () => {
    it("starts with null cursor and zero-value derivations", () => {
      const cursor = useTimelineCursor(makeBasicData());
      expect(cursor.cursorTime.value).toBeNull();
      expect(cursor.percentage.value).toBe(0);
      expect(cursor.snappedPoint.value).toBeNull();
      expect(cursor.cursorPosition.value).toBeNull();
      expect(cursor.activeMode.value).toBeNull();
      expect(cursor.cursorAtModeStart.value).toBe(false);
      expect(cursor.nearbyAnnotations.value).toEqual([]);
      expect(cursor.formattedTime.value).toBe("");
      expect(cursor.elapsed.value).toBe("");
    });

    it("modeGradientCSS is computed even without a cursor", () => {
      const cursor = useTimelineCursor(makeBasicData());
      expect(cursor.modeGradientCSS.value).toMatch(/linear-gradient/);
    });
  });

  describe("setting cursorTime directly", () => {
    it("updates all derived state when cursorTime is set", () => {
      const cursor = useTimelineCursor(makeBasicData());

      cursor.cursorTime.value = T0;
      expect(cursor.percentage.value).toBe(0);
      expect(cursor.snappedPoint.value).not.toBeNull();
      expect(cursor.snappedPoint.value!.index).toBe(0);
      expect(cursor.cursorPosition.value).toEqual([174.0, -36.0]);
      expect(cursor.activeMode.value?.mode).toBe("SAIL");
      expect(cursor.formattedTime.value).not.toBe("");
    });

    it("updates percentage at midpoint", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T2;
      expect(cursor.percentage.value).toBe(50);
    });

    it("finds nearby annotations", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T1;
      expect(cursor.nearbyAnnotations.value).toHaveLength(1);
      expect(cursor.nearbyAnnotations.value[0].type).toBe("TACK");
    });

    it("detects mode start near cursor", () => {
      const cursor = useTimelineCursor(makeBasicData());
      // T0 is exactly where SAIL mode starts
      cursor.cursorTime.value = T0;
      expect(cursor.cursorAtModeStart.value).toBe(true);
    });

    it("returns false for cursorAtModeStart away from start", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T1;
      expect(cursor.cursorAtModeStart.value).toBe(false);
    });

    it("clears derived state when cursorTime set back to null", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T2;
      expect(cursor.activeMode.value).not.toBeNull();

      cursor.cursorTime.value = null;
      expect(cursor.percentage.value).toBe(0);
      expect(cursor.snappedPoint.value).toBeNull();
      expect(cursor.activeMode.value).toBeNull();
      expect(cursor.nearbyAnnotations.value).toEqual([]);
    });
  });

  describe("setCursorFromPercentage", () => {
    it("sets cursor to journey start at 0%", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.setCursorFromPercentage(0);
      expect(cursor.cursorTime.value).toBe(new Date(T0_MS).toISOString());
      expect(cursor.percentage.value).toBe(0);
    });

    it("sets cursor to midpoint at 50%", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.setCursorFromPercentage(50);
      expect(cursor.percentage.value).toBe(50);
      expect(cursor.snappedPoint.value!.index).toBe(2); // T2 feature
    });

    it("sets cursor to journey end at 100%", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.setCursorFromPercentage(100);
      expect(cursor.percentage.value).toBe(100);
    });
  });

  describe("setCursorFromCoordinate", () => {
    it("snaps to the nearest track point by coordinate", () => {
      const cursor = useTimelineCursor(makeBasicData());
      // Close to T2 feature at [174.2, -36.2]
      cursor.setCursorFromCoordinate([174.19, -36.19]);
      expect(cursor.snappedPoint.value!.index).toBe(2);
      expect(cursor.cursorPosition.value).toEqual([174.2, -36.2]);
    });

    it("does not change cursor when features are empty", () => {
      const data = makeBasicData();
      data.features = makeCollection([]);
      const cursor = useTimelineCursor(data);
      cursor.setCursorFromCoordinate([174.0, -36.0]);
      expect(cursor.cursorTime.value).toBeNull();
    });
  });

  describe("resetCursor", () => {
    it("sets cursor to journey start time", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T3; // move somewhere
      cursor.resetCursor();
      expect(cursor.percentage.value).toBe(0);
      expect(cursor.cursorTime.value).toBe(new Date(T0_MS).toISOString());
    });
  });

  describe("clearCursor", () => {
    it("sets cursor time to null", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T3;
      cursor.clearCursor();
      expect(cursor.cursorTime.value).toBeNull();
      expect(cursor.percentage.value).toBe(0);
      expect(cursor.snappedPoint.value).toBeNull();
    });
  });

  describe("reactive data changes", () => {
    it("rebuilds engine when data ref changes", () => {
      const data = ref<TimelineData>(makeBasicData());
      const cursor = useTimelineCursor(data);

      cursor.cursorTime.value = T1;
      expect(cursor.activeMode.value?.mode).toBe("SAIL");

      // Change modes so T1 is now in ANCHOR mode
      data.value = {
        ...data.value,
        modes: [makeMode(1, "ANCHOR", T0, T4)],
      };

      expect(cursor.activeMode.value?.mode).toBe("ANCHOR");
    });

    it("updates gradient when modes change", () => {
      const data = ref<TimelineData>(makeBasicData());
      const cursor = useTimelineCursor(data);

      const gradient1 = cursor.modeGradientCSS.value;

      data.value = {
        ...data.value,
        modes: [],
      };

      expect(cursor.modeGradientCSS.value).not.toBe(gradient1);
      expect(cursor.modeGradientCSS.value).toBe("#f0f1f3");
    });

    it("updates nearby annotations when annotations change", () => {
      const data = ref<TimelineData>(makeBasicData());
      const cursor = useTimelineCursor(data);

      cursor.cursorTime.value = T1;
      expect(cursor.nearbyAnnotations.value).toHaveLength(1);

      // Remove all annotations
      data.value = {
        ...data.value,
        annotations: [],
      };

      expect(cursor.nearbyAnnotations.value).toHaveLength(0);
    });

    it("stepPoint moves to the next/previous GPS point", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T1;
      cursor.stepPoint(1);
      expect(cursor.cursorTime.value).toBe(T2);
      cursor.stepPoint(-1);
      expect(cursor.cursorTime.value).toBe(T1);
    });

    it("stepPoint seeds from journey start when cursor is null", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.stepPoint(1);
      // resetCursor sets to startMs → T0
      expect(cursor.cursorTime.value).toBe(new Date(T0_MS).toISOString());
    });

    it("stepPoint clamps at journey ends", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T0;
      cursor.stepPoint(-1);
      expect(cursor.cursorTime.value).toBe(T0);
      cursor.cursorTime.value = T4;
      cursor.stepPoint(1);
      expect(cursor.cursorTime.value).toBe(T4);
    });

    it("stepPoint jumps several points at once when given a count", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T0;
      cursor.stepPoint(1, 3);
      expect(cursor.cursorTime.value).toBe(T3);
      cursor.stepPoint(-1, 2);
      expect(cursor.cursorTime.value).toBe(T1);
    });

    it("stepPoint lands on the last point when the jump overshoots", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T1;
      cursor.stepPoint(1, 10);
      expect(cursor.cursorTime.value).toBe(T4);
      cursor.stepPoint(-1, 10);
      expect(cursor.cursorTime.value).toBe(T0);
    });

    it("stepModeBoundary jumps to next/previous mode boundary", () => {
      const cursor = useTimelineCursor(makeBasicData());
      // Boundaries (from modes): T0, T2, T4
      cursor.cursorTime.value = T1;
      cursor.stepModeBoundary(1);
      expect(cursor.cursorTime.value).toBe(new Date(T2).toISOString());
      cursor.stepModeBoundary(-1);
      expect(cursor.cursorTime.value).toBe(new Date(T0).toISOString());
    });

    it("stepModeBoundary is a no-op when no boundary exists in the requested direction", () => {
      const cursor = useTimelineCursor(makeBasicData());
      cursor.cursorTime.value = T4;
      cursor.stepModeBoundary(1);
      // T4 is itself a boundary; next must be strictly after → none
      expect(cursor.cursorTime.value).toBe(T4);
    });

    it("cursor stays stable when journey bounds change", () => {
      const data = ref<TimelineData>(makeBasicData());
      const cursor = useTimelineCursor(data);

      cursor.cursorTime.value = T2;
      const cursorBefore = cursor.cursorTime.value;

      // Extend the journey end by 4 hours — percentage should change, cursor stays
      data.value = {
        ...data.value,
        journeyEnd: "2024-04-01T16:00:00Z",
      };

      expect(cursor.cursorTime.value).toBe(cursorBefore);
      // Percentage should now be 25% instead of 50% (2h into 8h journey)
      expect(cursor.percentage.value).toBe(25);
    });
  });

  describe("snapMs", () => {
    const T1_MS = new Date(T1).getTime();
    const T2_MS = new Date(T2).getTime();
    const T4_MS = new Date(T4).getTime();

    it("returns the nearest fix time to an arbitrary moment", () => {
      const cursor = useTimelineCursor(makeBasicData());
      // Just after T1, well before T2 — snaps back to T1.
      expect(cursor.snapMs(T1_MS + 60_000)).toBe(T1_MS);
      // Just before T2 — snaps forward to T2.
      expect(cursor.snapMs(T2_MS - 60_000)).toBe(T2_MS);
    });

    it("clamps to the ends of the track", () => {
      const cursor = useTimelineCursor(makeBasicData());
      expect(cursor.snapMs(T0_MS - 86_400_000)).toBe(T0_MS);
      expect(cursor.snapMs(T4_MS + 86_400_000)).toBe(T4_MS);
    });

    it("returns null when there are no fixes to snap to", () => {
      const data = makeBasicData();
      data.features = makeCollection([]);
      const cursor = useTimelineCursor(data);
      expect(cursor.snapMs(T0_MS)).toBeNull();
    });
  });
});
