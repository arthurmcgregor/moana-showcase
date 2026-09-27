import type { Mode, Annotation, GeoJSONFeature } from "@/api/web";
import type { TimelineData, SnappedPoint, TimelineEngine } from "./types";
import { getModeColor } from "@/constants/journeyTypes";

const DEFAULT_ANNOTATION_WINDOW_MS = 2 * 60 * 1000; // ±2 minutes
const DEFAULT_MODE_COLOR = "#6c757d";

/**
 * Binary search for the index of the element nearest to `target` in a sorted array.
 * Returns -1 if the array is empty.
 */
function findNearestIndex(sorted: number[], target: number): number {
  if (sorted.length === 0) return -1;
  if (sorted.length === 1) return 0;

  let lo = 0;
  let hi = sorted.length - 1;

  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (sorted[mid] < target) lo = mid + 1;
    else hi = mid;
  }

  if (lo === 0) return 0;
  if (lo >= sorted.length) return sorted.length - 1;

  return Math.abs(sorted[lo - 1] - target) <= Math.abs(sorted[lo] - target) ? lo - 1 : lo;
}

/**
 * Find the first index where sorted[i] >= target. Returns the array length if
 * every element is below it.
 */
function lowerBound(sorted: number[], target: number): number {
  let lo = 0;
  let hi = sorted.length;

  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (sorted[mid] < target) lo = mid + 1;
    else hi = mid;
  }

  return lo;
}

/**
 * Find the last index where sorted[i] <= target. Returns -1 if none.
 */
function bisectRight(sorted: number[], target: number): number {
  if (sorted.length === 0) return -1;

  let lo = 0;
  let hi = sorted.length;

  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (sorted[mid] <= target) lo = mid + 1;
    else hi = mid;
  }

  return lo - 1;
}

interface PreparedModes {
  sorted: Mode[];
  startTimes: number[];
}

function prepareModes(modes: Mode[]): PreparedModes {
  const sorted = [...modes].sort(
    (a, b) => new Date(a.started_at).getTime() - new Date(b.started_at).getTime(),
  );
  const startTimes = sorted.map((m) => new Date(m.started_at).getTime());
  return { sorted, startTimes };
}

function findModeAtTime(preparedModes: PreparedModes, epochMs: number): Mode | null {
  const { sorted, startTimes } = preparedModes;
  if (sorted.length === 0) return null;

  const idx = bisectRight(startTimes, epochMs);
  if (idx < 0) return null;

  const mode = sorted[idx];
  const endMs = mode.ended_at ? new Date(mode.ended_at).getTime() : Infinity;
  return epochMs <= endMs ? mode : null;
}

export function createTimelineEngine(data: TimelineData): TimelineEngine {
  const startMs = new Date(data.journeyStart).getTime();

  // If no explicit end, use last feature timestamp
  const features = data.features.features;
  const explicitEnd = data.journeyEnd ? new Date(data.journeyEnd).getTime() : null;
  const lastFeatureTime =
    features.length > 0
      ? new Date(features[features.length - 1].properties.timestamp).getTime()
      : startMs;
  const endMs = explicitEnd ?? lastFeatureTime;

  const range = endMs - startMs;

  // Pre-compute sorted feature timestamps for binary search
  const featureTimestamps = features
    .map((f) => (f.properties?.timestamp ? new Date(f.properties.timestamp).getTime() : 0))
    .filter((t) => t > 0);

  // Pre-sort modes and extract start times
  const preparedModes = prepareModes(data.modes);

  // Pre-compute sorted unique mode boundary timestamps (each mode's start and
  // end). Used for Shift+arrow jumps — boundaries strictly inside the journey
  // are what the user wants to snap to.
  const modeBoundaries = (() => {
    const set = new Set<number>();
    for (const mode of preparedModes.sorted) {
      set.add(new Date(mode.started_at).getTime());
      if (mode.ended_at) set.add(new Date(mode.ended_at).getTime());
    }
    return [...set].sort((a, b) => a - b);
  })();

  const engine: TimelineEngine = {
    startMs,
    endMs,

    timeToPercentage(epochMs: number): number {
      if (range <= 0) return 0;
      return Math.max(0, Math.min(100, ((epochMs - startMs) / range) * 100));
    },

    percentageToTime(percentage: number): number {
      return startMs + (range * Math.max(0, Math.min(100, percentage))) / 100;
    },

    snapToPoint(epochMs: number): SnappedPoint | null {
      if (featureTimestamps.length === 0) return null;
      const idx = findNearestIndex(featureTimestamps, epochMs);
      if (idx < 0) return null;
      return { index: idx, feature: features[idx] };
    },

    snapToPointAtOrAfter(epochMs: number): SnappedPoint | null {
      if (featureTimestamps.length === 0) return null;
      const idx = lowerBound(featureTimestamps, epochMs);
      if (idx >= featureTimestamps.length) return null;
      return { index: idx, feature: features[idx] };
    },

    pointAtIndex(idx: number): SnappedPoint | null {
      if (idx < 0 || idx >= features.length) return null;
      return { index: idx, feature: features[idx] };
    },

    modeAtTime(epochMs: number): Mode | null {
      return findModeAtTime(preparedModes, epochMs);
    },

    modeBefore(epochMs: number): Mode | null {
      const active = findModeAtTime(preparedModes, epochMs);
      if (!active) return null;
      const activeStart = new Date(active.started_at).getTime();
      const { sorted, startTimes } = preparedModes;
      const idx = bisectRight(startTimes, activeStart - 1);
      return idx >= 0 ? sorted[idx] : null;
    },

    modeColorAtTime(epochMs: number): string {
      const mode = findModeAtTime(preparedModes, epochMs);
      return mode ? getModeColor(mode.mode) : DEFAULT_MODE_COLOR;
    },

    isNearModeStart(epochMs: number): boolean {
      const mode = findModeAtTime(preparedModes, epochMs);
      if (!mode) return false;

      // Snap both cursor and mode start to their nearest GPS points
      const cursorSnapped = engine.snapToPoint(epochMs);
      const modeStartMs = new Date(mode.started_at).getTime();
      const modeSnapped = engine.snapToPoint(modeStartMs);

      if (!cursorSnapped || !modeSnapped) return false;
      return cursorSnapped.index === modeSnapped.index;
    },

    modeBoundaryAfter(epochMs: number): number | null {
      for (const b of modeBoundaries) {
        if (b > epochMs) return b;
      }
      return null;
    },

    modeBoundaryBefore(epochMs: number): number | null {
      for (let i = modeBoundaries.length - 1; i >= 0; i--) {
        if (modeBoundaries[i] < epochMs) return modeBoundaries[i];
      }
      return null;
    },

    annotationsNear(epochMs: number, windowMs = DEFAULT_ANNOTATION_WINDOW_MS): Annotation[] {
      return data.annotations.filter(
        (a) => Math.abs(new Date(a.timestamp).getTime() - epochMs) <= windowMs,
      );
    },

    nearestPointToCoordinate(coord: [number, number]): SnappedPoint | null {
      if (features.length === 0) return null;

      const [clickLng, clickLat] = coord;
      let bestIdx = 0;
      let bestDist = Infinity;

      for (let i = 0; i < features.length; i++) {
        const [lng, lat] = features[i].geometry.coordinates;
        const dist = (lng - clickLng) ** 2 + (lat - clickLat) ** 2;
        if (dist < bestDist) {
          bestDist = dist;
          bestIdx = i;
        }
      }

      return { index: bestIdx, feature: features[bestIdx] };
    },

    modeGradientCSS(): string {
      const { sorted } = preparedModes;
      if (sorted.length === 0 || range <= 0) return "#f0f1f3";

      const stops: string[] = [];
      let lastPct = 0;

      for (const seg of sorted) {
        const segStart = new Date(seg.started_at).getTime();
        const segEnd = seg.ended_at ? new Date(seg.ended_at).getTime() : endMs;
        const startPct = Math.max(0, ((segStart - startMs) / range) * 100);
        const endPct = Math.min(100, ((segEnd - startMs) / range) * 100);
        const color = getModeColor(seg.mode) + "80";

        if (startPct > lastPct) {
          stops.push(`#f0f1f3 ${lastPct}%`, `#f0f1f3 ${startPct}%`);
        }
        stops.push(`${color} ${startPct}%`, `${color} ${endPct}%`);
        lastPct = endPct;
      }

      if (lastPct < 100) {
        stops.push(`#f0f1f3 ${lastPct}%`, `#f0f1f3 100%`);
      }

      return `linear-gradient(to right, ${stops.join(", ")})`;
    },

    modeColorsForFeatures(targetFeatures: GeoJSONFeature[]): string[] {
      // Sweep algorithm: walk through features and modes together
      const { sorted } = preparedModes;
      if (sorted.length === 0) return targetFeatures.map(() => DEFAULT_MODE_COLOR);

      const colors: string[] = new Array(targetFeatures.length);
      let modeIdx = 0;
      let currentModeEnd = -Infinity;
      let currentColor = DEFAULT_MODE_COLOR;

      for (let i = 0; i < targetFeatures.length; i++) {
        const ts = new Date(targetFeatures[i].properties.timestamp).getTime();

        // Advance mode pointer while next mode has started
        while (modeIdx < sorted.length - 1) {
          const nextStart = new Date(sorted[modeIdx + 1].started_at).getTime();
          if (nextStart <= ts) {
            modeIdx++;
            const endedAt = sorted[modeIdx].ended_at;
            currentModeEnd = endedAt ? new Date(endedAt).getTime() : Infinity;
            currentColor = getModeColor(sorted[modeIdx].mode);
          } else {
            break;
          }
        }

        // Check if first mode applies
        if (modeIdx === 0 && currentModeEnd === -Infinity) {
          const firstStart = new Date(sorted[0].started_at).getTime();
          if (firstStart <= ts) {
            const endedAt = sorted[0].ended_at;
            currentModeEnd = endedAt ? new Date(endedAt).getTime() : Infinity;
            currentColor = getModeColor(sorted[0].mode);
          }
        }

        colors[i] = ts <= currentModeEnd ? currentColor : DEFAULT_MODE_COLOR;
      }

      return colors;
    },
  };

  return engine;
}
