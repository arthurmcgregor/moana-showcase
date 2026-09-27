import type { GeoJSONFeature } from "@/api/web";
import type { TimelineTree } from "./timelineTree";

/**
 * What the journey screen is currently looking at. Selecting a leg or a run of
 * segments scopes the map, the stats and the deck to its time window; `journey`
 * is the whole day, and is what selecting the current selection again returns to.
 *
 * A segment selection is a list rather than one id because shift-click gathers a
 * run of them. The run is always contiguous in time — that is what lets every
 * consumer keep taking a single `TimeWindow` and never reason about a gap.
 */
export type Scope =
  | { kind: "journey" }
  | { kind: "leg"; id: number }
  | { kind: "segments"; ids: number[] };

export interface TimeWindow {
  startMs: number;
  /** Infinity while the scope is still running. */
  endMs: number;
}

export interface ScopeJourney {
  started_at: string;
  ended_at: string | null;
}

export const JOURNEY_SCOPE: Scope = { kind: "journey" };

export function scopesEqual(a: Scope, b: Scope): boolean {
  if (a.kind !== b.kind) return false;
  if (a.kind === "journey" || b.kind === "journey") return true;
  if (a.kind === "segments" && b.kind === "segments") {
    // By membership, not order. `scopeWindow` already resolves a run whatever
    // order its ids arrive in, and `?seg=6,4` typed by hand has to compare equal
    // to the same run selected in the tree or the toggle-off rule stops firing.
    // Segment ids are mode ids, so there is no sorting them back into time order.
    if (a.ids.length !== b.ids.length) return false;
    const mine = new Set(a.ids);
    return b.ids.every((id) => mine.has(id));
  }
  return a.kind === "leg" && b.kind === "leg" && a.id === b.id;
}

/** The segment ids a scope selects, for the rows that draw themselves selected. */
export function selectedSegmentIds(scope: Scope): ReadonlySet<number> {
  return scope.kind === "segments" ? new Set(scope.ids) : new Set();
}

/** Latest end known to the tree, or Infinity if anything is still running. */
function latestEndMs(tree: TimelineTree): number {
  if (!tree.segments.length) return Infinity;
  let end = -Infinity;
  for (const segment of tree.segments) {
    if (segment.endMs === null) return Infinity;
    if (segment.endMs > end) end = segment.endMs;
  }
  return end;
}

/**
 * The time window a scope covers. A leg or segment id that is no longer in the
 * tree — a stale `?leg=` in the URL, or a row deleted under us — falls back to
 * the whole journey rather than an empty window.
 *
 * A run of segments spans from the earliest start to the latest end, so a run
 * still resolves to one window even if the ids arrive out of order.
 */
export function scopeWindow(
  scope: Scope,
  tree: TimelineTree,
  journey: ScopeJourney,
): TimeWindow {
  if (scope.kind === "leg") {
    const leg = tree.entries.find((e) => e.kind === "leg" && e.leg.id === scope.id);
    if (leg) return { startMs: leg.startMs, endMs: leg.endMs ?? Infinity };
  } else if (scope.kind === "segments") {
    const ids = new Set(scope.ids);
    const chosen = tree.segments.filter((s) => ids.has(s.id));
    if (chosen.length) {
      let startMs = Infinity;
      let endMs = -Infinity;
      for (const segment of chosen) {
        if (segment.startMs < startMs) startMs = segment.startMs;
        // A run holding anything still running is open-ended.
        if (segment.endMs === null) endMs = Infinity;
        else if (endMs !== Infinity && segment.endMs > endMs) endMs = segment.endMs;
      }
      return { startMs, endMs };
    }
  }

  const startMs = new Date(journey.started_at).getTime();
  const endMs = journey.ended_at ? new Date(journey.ended_at).getTime() : latestEndMs(tree);
  return { startMs, endMs };
}

/**
 * Fixes inside a window, both ends inclusive. A fix sitting exactly on a
 * boundary belongs to the segments on either side of it: the leg from that fix
 * to its neighbour is real distance for both, so per-segment distances sum back
 * to the journey's rather than losing a leg at every boundary.
 */
export function featuresInWindow(
  features: GeoJSONFeature[],
  window: TimeWindow,
): GeoJSONFeature[] {
  return features.filter((f) => {
    const ms = new Date(f.properties.timestamp).getTime();
    return ms >= window.startMs && ms <= window.endMs;
  });
}

export function isInWindow(timestamp: string, window: TimeWindow): boolean {
  const ms = new Date(timestamp).getTime();
  return ms >= window.startMs && ms <= window.endMs;
}
