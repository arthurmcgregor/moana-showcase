/**
 * Stretches of a track where the boat kept sailing but nothing was recorded.
 *
 * A trace drawn straight through one of these is a claim about speed nobody
 * measured — on the map the absence shows as missing fixes, but a line has to be
 * broken deliberately or it interpolates the gap away.
 */

export interface SampleGap {
  /** Timestamp of the last sample before the gap. */
  startMs: number;
  /** Timestamp of the first sample after it. */
  endMs: number;
}

/** Below this, a break reads as noise rather than as an absence. */
const MIN_GAP_MS = 30_000;

/** How many times the usual interval a silence must run before it is a gap. */
const CADENCE_MULTIPLE = 10;

/**
 * The interval this track was recorded at. The median rather than the mean: one
 * eleven-minute dropout would drag a mean far enough to hide every other gap.
 */
export function medianIntervalMs(sortedMs: readonly number[]): number {
  if (sortedMs.length < 2) return 0;
  const intervals: number[] = [];
  for (let i = 1; i < sortedMs.length; i++) intervals.push(sortedMs[i] - sortedMs[i - 1]);
  intervals.sort((a, b) => a - b);
  const mid = intervals.length >> 1;
  return intervals.length % 2 === 0
    ? (intervals[mid - 1] + intervals[mid]) / 2
    : intervals[mid];
}

/**
 * What counts as a dropout on this track. Derived from its own cadence, because
 * a device logging every minute is not in trouble at the interval that would be
 * a long silence from one logging every second — and floored, because under half
 * a minute a boat's speed has not gone anywhere unobserved.
 */
export function gapThresholdMs(sortedMs: readonly number[]): number {
  return Math.max(medianIntervalMs(sortedMs) * CADENCE_MULTIPLE, MIN_GAP_MS);
}

/** The silences in an ascending series of sample times. */
export function findGaps(sortedMs: readonly number[], thresholdMs: number): SampleGap[] {
  const gaps: SampleGap[] = [];
  for (let i = 1; i < sortedMs.length; i++) {
    if (sortedMs[i] - sortedMs[i - 1] >= thresholdMs) {
      gaps.push({ startMs: sortedMs[i - 1], endMs: sortedMs[i] });
    }
  }
  return gaps;
}

/**
 * Split a series into the runs between gaps.
 *
 * The gaps are found on the raw samples and applied here, rather than being
 * re-derived from `items`: a lane downsamples to one point per column, so on a
 * long journey the spacing between drawn points is itself minutes wide and would
 * read as a dropout everywhere. A run therefore breaks only where a real silence
 * sits between two of its points.
 */
export function splitAtGaps<T>(
  items: readonly T[],
  msOf: (item: T) => number,
  gaps: readonly SampleGap[],
): T[][] {
  if (!items.length) return [];
  if (!gaps.length) return [[...items]];

  const runs: T[][] = [];
  let run: T[] = [items[0]];
  let next = 0;

  for (let i = 1; i < items.length; i++) {
    const from = msOf(items[i - 1]);
    const to = msOf(items[i]);
    while (next < gaps.length && gaps[next].startMs < from) next++;
    if (next < gaps.length && gaps[next].startMs >= from && gaps[next].endMs <= to) {
      runs.push(run);
      run = [];
    }
    run.push(items[i]);
  }

  runs.push(run);
  return runs;
}
