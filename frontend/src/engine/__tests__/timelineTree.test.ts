import { describe, it, expect } from "vitest";
import {
  buildTimelineTree,
  claimedSegmentIds,
  segmentAtMs,
  segmentsInLegRange,
  type TreeLeg,
  type TreeSegment,
} from "../timelineTree";
import type { Mode, Leg, Annotation } from "@/api/web";

// ── Helpers ──────────────────────────────────────────────────────────────────

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

function makeAnnotation(
  id: number,
  type: string,
  timestamp: string,
  notes = "",
): Annotation {
  return {
    id,
    vessel: 1,
    type: type as Annotation["type"],
    timestamp,
    notes,
    metadata: {},
  };
}

const T0 = "2024-04-01T08:00:00Z";
const T1 = "2024-04-01T09:00:00Z";
const T2 = "2024-04-01T10:00:00Z";
const T3 = "2024-04-01T11:00:00Z";
const T4 = "2024-04-01T12:00:00Z";

const HOUR = 60 * 60 * 1000;

function legs(entries: ReturnType<typeof buildTimelineTree>["entries"]): TreeLeg[] {
  return entries.filter((e): e is TreeLeg => e.kind === "leg");
}

function topLevelSegments(
  entries: ReturnType<typeof buildTimelineTree>["entries"],
): TreeSegment[] {
  return entries.filter((e): e is TreeSegment => e.kind === "mode");
}

// ── Segments ─────────────────────────────────────────────────────────────────

describe("buildTimelineTree — segments", () => {
  it("sorts modes by start and derives durations", () => {
    const tree = buildTimelineTree(
      [makeMode(2, "MOTOR", T2, T4), makeMode(1, "SAIL", T0, T2)],
      [],
      [],
    );

    expect(tree.segments.map((s) => s.id)).toEqual([1, 2]);
    expect(tree.segments[0].durationMs).toBe(2 * HOUR);
    expect(tree.segments[0].startMs).toBe(new Date(T0).getTime());
    expect(tree.segments[0].endedAt).toBe(T2);
  });

  it("leaves an ongoing segment with a null end and duration", () => {
    const tree = buildTimelineTree([makeMode(1, "SAIL", T0)], [], []);

    expect(tree.segments[0].endMs).toBeNull();
    expect(tree.segments[0].durationMs).toBeNull();
    expect(tree.segments[0].endedAt).toBeNull();
  });

  it("takes maxDurationMs from the longest segment anywhere in the journey", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T1), makeMode(2, "MOTOR", T1, T4)],
      [makeLeg(1, "Leg", T1, T4)],
      [],
    );

    // The longest segment is nested inside a leg — the baseline still sees it.
    expect(tree.maxDurationMs).toBe(3 * HOUR);
  });

  it("ignores ongoing segments when computing the baseline", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T1), makeMode(2, "MOTOR", T1)],
      [],
      [],
    );

    expect(tree.maxDurationMs).toBe(HOUR);
  });
});

// ── Annotation bucketing ─────────────────────────────────────────────────────

describe("buildTimelineTree — annotations", () => {
  const modes = [makeMode(1, "SAIL", T0, T2), makeMode(2, "MOTOR", T2, T4)];

  it("buckets an annotation into the segment containing it", () => {
    const tree = buildTimelineTree(modes, [], [makeAnnotation(7, "TACK", T1)]);

    expect(tree.segments[0].chips.map((a) => a.id)).toEqual([7]);
    expect(tree.segments[1].chips).toEqual([]);
  });

  it("splits chips from comments on whether the annotation has notes", () => {
    const tree = buildTimelineTree(modes, [], [
      makeAnnotation(7, "TACK", T1),
      makeAnnotation(8, "NOTE", T1, "Wind backed"),
      makeAnnotation(9, "NOTE", T1, "   "),
    ]);

    expect(tree.segments[0].chips.map((a) => a.id)).toEqual([7, 9]);
    expect(tree.segments[0].comments.map((a) => a.id)).toEqual([8]);
  });

  it("puts an annotation on a boundary in the later segment", () => {
    const tree = buildTimelineTree(modes, [], [makeAnnotation(7, "TACK", T2)]);

    expect(tree.segments[0].chips).toEqual([]);
    expect(tree.segments[1].chips.map((a) => a.id)).toEqual([7]);
  });

  it("drops an annotation outside every segment", () => {
    const tree = buildTimelineTree(modes, [], [
      makeAnnotation(7, "TACK", "2024-04-01T07:00:00Z"),
      makeAnnotation(8, "TACK", "2024-04-01T13:00:00Z"),
    ]);

    expect(tree.segments.flatMap((s) => [...s.chips, ...s.comments])).toEqual([]);
  });

  it("keeps annotations in time order within a segment", () => {
    const tree = buildTimelineTree(modes, [], [
      makeAnnotation(9, "GYBE", "2024-04-01T09:30:00Z"),
      makeAnnotation(7, "TACK", T1),
    ]);

    expect(tree.segments[0].chips.map((a) => a.id)).toEqual([7, 9]);
  });
});

// ── Leg grouping ─────────────────────────────────────────────────────────────

describe("buildTimelineTree — leg grouping", () => {
  it("returns a flat list of segments when there are no legs", () => {
    const tree = buildTimelineTree([makeMode(1, "SAIL", T0, T2)], [], []);

    expect(tree.entries).toHaveLength(1);
    expect(tree.entries[0].kind).toBe("mode");
  });

  it("nests a segment in the leg containing its midpoint", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T2), makeMode(2, "MOTOR", T2, T4)],
      [makeLeg(10, "Afternoon", T2, T4)],
      [],
    );

    expect(topLevelSegments(tree.entries).map((s) => s.id)).toEqual([1]);
    expect(legs(tree.entries)[0].segments.map((s) => s.id)).toEqual([2]);
  });

  it("assigns by midpoint, not by overlap", () => {
    // Segment runs 08:00–10:00, leg starts 09:30 — the midpoint (09:00) is
    // outside the leg, so the segment stays at top level.
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T2)],
      [makeLeg(10, "Late", "2024-04-01T09:30:00Z", T4)],
      [],
    );

    expect(topLevelSegments(tree.entries).map((s) => s.id)).toEqual([1]);
    expect(legs(tree.entries)[0].segments).toEqual([]);
  });

  it("treats a leg with no end as open", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T2, T4)],
      [makeLeg(10, "Ongoing", T1, null)],
      [],
    );

    expect(legs(tree.entries)[0].endMs).toBeNull();
    expect(legs(tree.entries)[0].segments.map((s) => s.id)).toEqual([1]);
  });

  it("gives a segment to the first leg claiming it", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T2)],
      [makeLeg(10, "First", T0, T4), makeLeg(11, "Overlapping", T0, T4)],
      [],
    );

    expect(legs(tree.entries)[0].segments.map((s) => s.id)).toEqual([1]);
    expect(legs(tree.entries)[1].segments).toEqual([]);
  });

  it("interleaves unassigned segments with legs by time", () => {
    const tree = buildTimelineTree(
      [
        makeMode(1, "SAIL", T0, T1),
        makeMode(2, "MOTOR", T1, T2),
        makeMode(3, "SAIL", T3, T4),
      ],
      [makeLeg(10, "Middle", T1, T3)],
      [],
    );

    expect(tree.entries.map((e) => (e.kind === "leg" ? `leg-${e.leg.id}` : `seg-${e.id}`)))
      .toEqual(["seg-1", "leg-10", "seg-3"]);
  });

  it("keeps the same segment objects in the flat list and the tree", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T2, T4)],
      [makeLeg(10, "Leg", T1, T4)],
      [],
    );

    expect(legs(tree.entries)[0].segments[0]).toBe(tree.segments[0]);
  });
});

// ── Pending leg edit ─────────────────────────────────────────────────────────

describe("buildTimelineTree — leg edit override", () => {
  const modes = [
    makeMode(1, "SAIL", T0, T1),
    makeMode(2, "MOTOR", T1, T2),
    makeMode(3, "SAIL", T2, T3),
  ];
  const legList = [makeLeg(10, "Morning", T0, T1)];

  it("takes membership from the pending selection, not the stored bounds", () => {
    const tree = buildTimelineTree(modes, legList, [], {
      legId: 10,
      segmentIds: new Set([2, 3]),
      bounds: { start: T1, end: T3 },
    });

    const leg = legs(tree.entries)[0];
    expect(leg.isEditing).toBe(true);
    expect(leg.segments.map((s) => s.id)).toEqual([2, 3]);
    expect(topLevelSegments(tree.entries).map((s) => s.id)).toEqual([1]);
  });

  it("previews the pending bounds", () => {
    const tree = buildTimelineTree(modes, legList, [], {
      legId: 10,
      segmentIds: new Set([2, 3]),
      bounds: { start: T1, end: T3 },
    });

    const leg = legs(tree.entries)[0];
    expect(leg.startedAt).toBe(T1);
    expect(leg.endedAt).toBe(T3);
    expect(leg.startMs).toBe(new Date(T1).getTime());
  });

  it("shows stored bounds but claims nothing when the selection is empty", () => {
    const tree = buildTimelineTree(modes, legList, [], {
      legId: 10,
      segmentIds: new Set(),
      bounds: null,
    });

    const leg = legs(tree.entries)[0];
    expect(leg.startedAt).toBe(T0);
    expect(leg.segments).toEqual([]);
    expect(topLevelSegments(tree.entries).map((s) => s.id)).toEqual([1, 2, 3]);
  });

  it("leaves other legs on midpoint membership", () => {
    const tree = buildTimelineTree(
      modes,
      [makeLeg(10, "Morning", T0, T1), makeLeg(11, "Later", T2, T3)],
      [],
      { legId: 10, segmentIds: new Set([1]), bounds: { start: T0, end: T1 } },
    );

    const [editing, other] = legs(tree.entries);
    expect(editing.segments.map((s) => s.id)).toEqual([1]);
    expect(other.isEditing).toBe(false);
    expect(other.segments.map((s) => s.id)).toEqual([3]);
  });
});

// ── Selection helpers ────────────────────────────────────────────────────────

describe("segmentsInLegRange", () => {
  it("selects by midpoint against the stored bounds", () => {
    const tree = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T1), makeMode(2, "MOTOR", T1, T2)],
      [],
      [],
    );

    const inRange = segmentsInLegRange(tree.segments, makeLeg(10, "Leg", T1, T3));
    expect(inRange.map((s) => s.id)).toEqual([2]);
  });
});

describe("claimedSegmentIds", () => {
  const tree = buildTimelineTree(
    [makeMode(1, "SAIL", T0, T1), makeMode(2, "MOTOR", T1, T2), makeMode(3, "SAIL", T2, T3)],
    [],
    [],
  );
  const legList = [makeLeg(10, "First", T0, T1), makeLeg(11, "Second", T2, T3)];

  it("claims segments held by other legs", () => {
    expect([...claimedSegmentIds(tree.segments, legList, null)].sort()).toEqual([1, 3]);
  });

  it("frees the segments of the leg being edited", () => {
    expect([...claimedSegmentIds(tree.segments, legList, 11)]).toEqual([1]);
  });
});

describe("segmentAtMs", () => {
  const segments = buildTimelineTree(
    [makeMode(1, "SAIL", T0, T1), makeMode(2, "MOTOR", T1, T2), makeMode(3, "SAIL", T2, T3)],
    [],
    [],
  ).segments;

  const at = (iso: string) => segmentAtMs(segments, new Date(iso).getTime())?.id ?? null;

  it("resolves a shared boundary to the segment that starts on it", () => {
    // The bug this guards: clicking a mode in the tree snaps the playhead to its
    // first instant, and a closed test handed that instant to the mode before it.
    expect(at(T1)).toBe(2);
    expect(at(T2)).toBe(3);
  });

  it("resolves an interior moment to the segment containing it", () => {
    expect(segmentAtMs(segments, new Date(T0).getTime() + HOUR / 2)?.id).toBe(1);
  });

  it("gives the journey's last instant to the segment that ends on it", () => {
    expect(at(T3)).toBe(3);
  });

  it("gives a moment in a gap to the mode that just finished", () => {
    const gapped = buildTimelineTree(
      [makeMode(1, "SAIL", T0, T1), makeMode(2, "MOTOR", T2, T3)],
      [],
      [],
    ).segments;
    expect(segmentAtMs(gapped, new Date(T1).getTime())?.id).toBe(1);
    expect(segmentAtMs(gapped, new Date(T1).getTime() + HOUR / 2)).toBeNull();
  });

  it("has nothing before the first segment", () => {
    expect(segmentAtMs(segments, new Date(T0).getTime() - HOUR)).toBeNull();
  });

  it("runs an open-ended segment to infinity", () => {
    const open = buildTimelineTree([makeMode(1, "SAIL", T0)], [], []).segments;
    expect(segmentAtMs(open, new Date(T4).getTime())?.id).toBe(1);
  });
});
