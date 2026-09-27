import type { Mode, Leg, Annotation, AnnotationType } from "@/api/web";
import { modeTypes, annotationTypes, getModeColor } from "@/constants/journeyTypes";

const DEFAULT_ANNOTATION_COLOR = "#6c757d";

/**
 * A journey's timeline as a two-level tree: leg groups with mode segments nested
 * inside them, interleaved by time with the segments that belong to no leg.
 *
 * Everything here is time and identity only — no formatted strings. Callers
 * format from `startedAt` / `startMs` / `durationMs` so the same tree can back
 * the breakdown list, the deck and the map.
 */

export interface TreeAnnotation {
  id: number;
  type: AnnotationType;
  label: string;
  color: string;
  timestamp: string;
  timeMs: number;
  notes: string;
}

export interface TreeSegment {
  kind: "mode";
  id: number;
  mode: string;
  label: string;
  color: string;
  startMs: number;
  /** null while the segment is still running. */
  endMs: number | null;
  durationMs: number | null;
  startedAt: string;
  endedAt: string | null;
  /** Annotations inside this segment with no notes — rendered as a chip row. */
  chips: TreeAnnotation[];
  /** Annotations inside this segment carrying notes — rendered as comments. */
  comments: TreeAnnotation[];
}

export interface TreeLeg {
  kind: "leg";
  leg: Leg;
  /** Effective bounds: the pending edit's, when one overrides this leg. */
  startMs: number;
  endMs: number | null;
  startedAt: string;
  endedAt: string | null;
  segments: TreeSegment[];
  isEditing: boolean;
}

export type TreeEntry = TreeSegment | TreeLeg;

export interface TimelineTree {
  /** Top-level rows in time order: leg groups and unassigned segments. */
  entries: TreeEntry[];
  /** Every segment, sorted by start — the same objects nested in `entries`. */
  segments: TreeSegment[];
  /** Longest segment in the journey; the shared baseline for duration bars. */
  maxDurationMs: number;
}

/**
 * Membership override for a leg being edited, so the tree can preview the
 * pending selection before it is saved. `bounds` is null when the selection is
 * empty — the leg then shows its stored range but claims no segments.
 */
export interface LegEditOverride {
  legId: number;
  segmentIds: ReadonlySet<number>;
  bounds: { start: string; end: string } | null;
}

function toMs(iso: string): number {
  return new Date(iso).getTime();
}

/**
 * Segments sorted by start, with annotations bucketed into the one containing
 * them. Exported alongside `groupSegments` because a caller whose leg-edit
 * override is itself derived from the segments cannot go through
 * `buildTimelineTree` without a cycle.
 */
export function buildTimelineSegments(modes: Mode[], annotations: Annotation[]): TreeSegment[] {
  const segments: TreeSegment[] = [...modes]
    .sort((a, b) => toMs(a.started_at) - toMs(b.started_at))
    .map((m) => {
      const startMs = toMs(m.started_at);
      const endMs = m.ended_at ? toMs(m.ended_at) : null;
      return {
        kind: "mode" as const,
        id: m.id,
        mode: m.mode,
        label: modeTypes[m.mode]?.label ?? m.mode,
        color: getModeColor(m.mode),
        startMs,
        endMs,
        durationMs: endMs ? endMs - startMs : null,
        startedAt: m.started_at,
        endedAt: m.ended_at ?? null,
        chips: [] as TreeAnnotation[],
        comments: [] as TreeAnnotation[],
      };
    });

  const sortedAnnotations = [...annotations].sort(
    (a, b) => toMs(a.timestamp) - toMs(b.timestamp),
  );

  for (const ann of sortedAnnotations) {
    const timeMs = toMs(ann.timestamp);
    const info = annotationTypes[ann.type] ?? {
      label: ann.type,
      color: DEFAULT_ANNOTATION_COLOR,
    };
    const marker: TreeAnnotation = {
      id: ann.id,
      type: ann.type,
      label: info.label,
      color: info.color,
      timestamp: ann.timestamp,
      timeMs,
      notes: ann.notes ?? "",
    };
    const owner = segmentAtMs(segments, timeMs);
    if (owner) {
      if (marker.notes.trim()) owner.comments.push(marker);
      else owner.chips.push(marker);
    }
  }

  return segments;
}

/**
 * The segment a moment falls in. Half-open `[startMs, endMs)`, so the instant of
 * a mode change belongs to the mode it starts, not the one it ends — a closed
 * test resolves a shared boundary to whichever segment comes first in the array,
 * which is always the outgoing one.
 *
 * Nothing starts at the journey's end, so a moment that lands in no segment
 * falls back to the segment ending on it. That covers the last instant of the
 * day, and a moment in a gap between modes keeps resolving to the mode that just
 * finished, as it did before.
 */
export function segmentAtMs(segments: TreeSegment[], atMs: number): TreeSegment | null {
  const containing = segments.find((s) => atMs >= s.startMs && atMs < (s.endMs ?? Infinity));
  if (containing) return containing;
  return segments.find((s) => s.endMs === atMs) ?? null;
}

/**
 * A segment belongs to the leg whose range contains its midpoint — so a segment
 * straddling a leg boundary lands on the side it spent most of its time.
 */
function midpointOf(segment: TreeSegment): number {
  return segment.endMs ? (segment.startMs + segment.endMs) / 2 : segment.startMs;
}

/** Nest segments under the legs that claim them, interleaving the rest by time. */
export function groupSegments(
  segments: TreeSegment[],
  legs: Leg[],
  override: LegEditOverride | null = null,
): TimelineTree {
  let maxDurationMs = 0;
  for (const segment of segments) {
    if (segment.durationMs && segment.durationMs > maxDurationMs) {
      maxDurationMs = segment.durationMs;
    }
  }

  if (!legs.length) {
    return { entries: [...segments], segments, maxDurationMs };
  }

  const legEntries: TreeLeg[] = [...legs]
    .sort((a, b) => toMs(a.started_at) - toMs(b.started_at))
    .map((leg) => {
      const isEditing = override !== null && leg.id === override.legId;
      const pending = isEditing ? override.bounds : null;
      const startedAt = pending ? pending.start : leg.started_at;
      const endedAt = pending ? pending.end : leg.ended_at;
      return {
        kind: "leg" as const,
        leg,
        startMs: toMs(startedAt),
        endMs: endedAt ? toMs(endedAt) : null,
        startedAt,
        endedAt,
        segments: [],
        isEditing,
      };
    });

  const assigned = new Set<number>();

  for (const segment of segments) {
    for (const entry of legEntries) {
      // The leg under edit takes exactly the pending selection; every other leg
      // claims by midpoint.
      const matches = entry.isEditing
        ? (override?.segmentIds.has(segment.id) ?? false)
        : midpointOf(segment) >= entry.startMs && midpointOf(segment) < (entry.endMs ?? Infinity);
      if (matches) {
        entry.segments.push(segment);
        assigned.add(segment.id);
        break;
      }
    }
  }

  const entries: TreeEntry[] = [
    ...segments.filter((s) => !assigned.has(s.id)),
    ...legEntries,
  ];
  entries.sort((a, b) => a.startMs - b.startMs);

  return { entries, segments, maxDurationMs };
}

export function buildTimelineTree(
  modes: Mode[],
  legs: Leg[],
  annotations: Annotation[],
  override: LegEditOverride | null = null,
): TimelineTree {
  return groupSegments(buildTimelineSegments(modes, annotations), legs, override);
}

/** Segments whose midpoint falls inside a leg's stored range. */
export function segmentsInLegRange(segments: TreeSegment[], leg: Leg): TreeSegment[] {
  const start = toMs(leg.started_at);
  const end = leg.ended_at ? toMs(leg.ended_at) : Infinity;
  return segments.filter((s) => midpointOf(s) >= start && midpointOf(s) < end);
}

/**
 * Segments claimed by a leg other than the one being edited — unselectable while
 * editing, since a segment cannot belong to two legs.
 */
export function claimedSegmentIds(
  segments: TreeSegment[],
  legs: Leg[],
  editingLegId: number | null,
): Set<number> {
  const others = legs
    .filter((leg) => leg.id !== editingLegId)
    .map((leg) => ({
      start: toMs(leg.started_at),
      end: leg.ended_at ? toMs(leg.ended_at) : Infinity,
    }));

  const claimed = new Set<number>();
  for (const segment of segments) {
    const mid = midpointOf(segment);
    if (others.some((range) => mid >= range.start && mid < range.end)) {
      claimed.add(segment.id);
    }
  }
  return claimed;
}
