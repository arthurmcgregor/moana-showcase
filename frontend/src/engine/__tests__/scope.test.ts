import { describe, it, expect } from "vitest";
import { buildTimelineTree } from "../timelineTree";
import {
  featuresInWindow,
  isInWindow,
  scopeWindow,
  scopesEqual,
  selectedSegmentIds,
} from "../scope";
import type { Mode, Leg, GeoJSONFeature } from "@/api/web";

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

function makeLeg(id: number, name: string, started_at: string, ended_at: string | null): Leg {
  return { id, journey: 1, name, started_at, ended_at, notes: "", duration: null };
}

function makeFeature(timestamp: string): GeoJSONFeature {
  return {
    type: "Feature",
    geometry: { type: "Point", coordinates: [174, -36] },
    properties: { timestamp, speed: 5, track_id: 1 },
  };
}

const T0 = "2024-04-01T08:00:00Z";
const T1 = "2024-04-01T09:00:00Z";
const T2 = "2024-04-01T10:00:00Z";
const T3 = "2024-04-01T11:00:00Z";
const T4 = "2024-04-01T12:00:00Z";

const ms = (iso: string) => new Date(iso).getTime();

const journey = { started_at: T0, ended_at: T4 };

const tree = buildTimelineTree(
  [makeMode(1, "SAIL", T0, T2), makeMode(2, "MOTOR", T2, T4)],
  [makeLeg(10, "Afternoon", T2, T4)],
  [],
);

describe("scopeWindow", () => {
  it("returns the journey bounds for journey scope", () => {
    expect(scopeWindow({ kind: "journey" }, tree, journey)).toEqual({
      startMs: ms(T0),
      endMs: ms(T4),
    });
  });

  it("returns a leg's bounds", () => {
    expect(scopeWindow({ kind: "leg", id: 10 }, tree, journey)).toEqual({
      startMs: ms(T2),
      endMs: ms(T4),
    });
  });

  it("returns a segment's bounds", () => {
    expect(scopeWindow({ kind: "segments", ids: [1] }, tree, journey)).toEqual({
      startMs: ms(T0),
      endMs: ms(T2),
    });
  });

  it("spans a run of segments from its first start to its last end", () => {
    expect(scopeWindow({ kind: "segments", ids: [1, 2] }, tree, journey)).toEqual({
      startMs: ms(T0),
      endMs: ms(T4),
    });
  });

  it("spans a run whose ids arrive out of order", () => {
    expect(scopeWindow({ kind: "segments", ids: [2, 1] }, tree, journey)).toEqual({
      startMs: ms(T0),
      endMs: ms(T4),
    });
  });

  it("ignores ids that are no longer in the tree but keeps the ones that are", () => {
    expect(scopeWindow({ kind: "segments", ids: [1, 999] }, tree, journey)).toEqual({
      startMs: ms(T0),
      endMs: ms(T2),
    });
  });

  it("falls back to the journey when the id is stale", () => {
    const whole = { startMs: ms(T0), endMs: ms(T4) };
    expect(scopeWindow({ kind: "leg", id: 999 }, tree, journey)).toEqual(whole);
    expect(scopeWindow({ kind: "segments", ids: [999] }, tree, journey)).toEqual(whole);
    expect(scopeWindow({ kind: "segments", ids: [] }, tree, journey)).toEqual(whole);
  });

  it("leaves an ongoing scope open-ended", () => {
    const ongoing = buildTimelineTree(
      [makeMode(1, "SAIL", T0)],
      [makeLeg(10, "Ongoing", T0, null)],
      [],
    );
    const openJourney = { started_at: T0, ended_at: null };

    expect(scopeWindow({ kind: "leg", id: 10 }, ongoing, openJourney).endMs).toBe(Infinity);
    expect(scopeWindow({ kind: "segments", ids: [1] }, ongoing, openJourney).endMs).toBe(Infinity);
    expect(scopeWindow({ kind: "journey" }, ongoing, openJourney).endMs).toBe(Infinity);
  });

  it("opens a run's end when anything in it is still running", () => {
    const ongoing = buildTimelineTree([makeMode(1, "SAIL", T0, T2), makeMode(2, "MOTOR", T2)], [], []);
    const openJourney = { started_at: T0, ended_at: null };
    expect(scopeWindow({ kind: "segments", ids: [1, 2] }, ongoing, openJourney).endMs).toBe(Infinity);
  });

  it("ends an unfinished journey at its last known segment end", () => {
    expect(scopeWindow({ kind: "journey" }, tree, { started_at: T0, ended_at: null })).toEqual({
      startMs: ms(T0),
      endMs: ms(T4),
    });
  });
});

describe("featuresInWindow", () => {
  const features = [T0, T1, T2, T3, T4].map(makeFeature);

  it("includes both boundaries so adjacent scopes share the fix between them", () => {
    const first = featuresInWindow(features, { startMs: ms(T0), endMs: ms(T2) });
    const second = featuresInWindow(features, { startMs: ms(T2), endMs: ms(T4) });

    expect(first.map((f) => f.properties.timestamp)).toEqual([T0, T1, T2]);
    expect(second.map((f) => f.properties.timestamp)).toEqual([T2, T3, T4]);
  });

  it("excludes fixes outside the window", () => {
    const inside = featuresInWindow(features, { startMs: ms(T1), endMs: ms(T3) });
    expect(inside.map((f) => f.properties.timestamp)).toEqual([T1, T2, T3]);
  });

  it("takes everything from an open-ended window", () => {
    expect(featuresInWindow(features, { startMs: ms(T1), endMs: Infinity })).toHaveLength(4);
  });
});

describe("isInWindow", () => {
  it("matches featuresInWindow at the boundaries", () => {
    const window = { startMs: ms(T1), endMs: ms(T3) };
    expect(isInWindow(T1, window)).toBe(true);
    expect(isInWindow(T3, window)).toBe(true);
    expect(isInWindow(T0, window)).toBe(false);
    expect(isInWindow(T4, window)).toBe(false);
  });
});

describe("scopesEqual", () => {
  it("compares kind and id", () => {
    expect(scopesEqual({ kind: "journey" }, { kind: "journey" })).toBe(true);
    expect(scopesEqual({ kind: "leg", id: 1 }, { kind: "leg", id: 1 })).toBe(true);
    expect(scopesEqual({ kind: "leg", id: 1 }, { kind: "leg", id: 2 })).toBe(false);
    expect(scopesEqual({ kind: "leg", id: 1 }, { kind: "segments", ids: [1] })).toBe(false);
    expect(scopesEqual({ kind: "journey" }, { kind: "leg", id: 1 })).toBe(false);
  });

  it("compares a run by its members", () => {
    expect(scopesEqual({ kind: "segments", ids: [1, 2] }, { kind: "segments", ids: [1, 2] })).toBe(true);
    expect(scopesEqual({ kind: "segments", ids: [1, 2] }, { kind: "segments", ids: [1] })).toBe(false);
    expect(scopesEqual({ kind: "segments", ids: [1, 2] }, { kind: "segments", ids: [1, 3] })).toBe(false);
  });

  it("does not care what order a run's ids arrive in", () => {
    // `?seg=2,1` typed by hand is the same selection the tree would write as 1,2,
    // and has to compare equal or selecting it again stops returning to the day.
    expect(scopesEqual({ kind: "segments", ids: [2, 1] }, { kind: "segments", ids: [1, 2] })).toBe(true);
  });
});

describe("selectedSegmentIds", () => {
  it("is the run's members, and empty for anything else", () => {
    expect([...selectedSegmentIds({ kind: "segments", ids: [3, 4] })]).toEqual([3, 4]);
    expect(selectedSegmentIds({ kind: "leg", id: 1 }).size).toBe(0);
    expect(selectedSegmentIds({ kind: "journey" }).size).toBe(0);
  });
});
