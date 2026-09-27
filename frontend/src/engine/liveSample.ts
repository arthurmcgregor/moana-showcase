import type { GeoJSONFeature, WeatherSummarySeriesPoint } from "@/api/web";

/**
 * What was true at one moment, as against what was true over a stretch.
 *
 * The tide has always been read this way — the height under the playhead, not
 * the day's mean — while speed, wind and temperature were only ever averaged
 * over the scope. The panel was then answering two different questions in the
 * same row of tiles. These give the other stats the same instantaneous reading,
 * so a tile can show both: the value now, and the spread it sat in.
 *
 * Both are gap-aware. A playhead parked in a recording silence or between two
 * distant station reports reads nothing rather than borrowing a number from
 * whenever the instruments were last awake.
 */

/** Fixes arrive about once a second; a minute either side is generous. */
const FIX_TOLERANCE_MS = 60_000;

/**
 * How much wall-clock time one reading of the panel is allowed to stand for.
 *
 * The instruments are read at the playhead, and during replay the playhead moves
 * at the multiplier — so at 300× a per-fix reading changes three hundred times
 * per second of journey time and the panel is a blur of digits that cannot be
 * read and cannot be ignored. The fix is not to slow the replay but to widen
 * what a reading covers: one number standing for `READOUT_CADENCE_MS` of the
 * viewer's own time, whatever that is worth in journey time.
 *
 * Five readings a second. Faster than that and the digits smear; slower and a 1×
 * replay feels like the panel has stopped answering.
 */
export const READOUT_CADENCE_MS = 200;

/**
 * The journey time one reading covers at a replay multiplier — the width of the
 * averaging window and, because they are the same number, the step the readout's
 * moment advances in.
 *
 * A multiplier of zero means the playhead is not running: a parked or scrubbed
 * playhead is a deliberate act of pointing at one moment, and averaging over it
 * would answer a question nobody asked.
 */
export function readoutWindowMs(multiplier: number): number {
  return Math.max(0, multiplier) * READOUT_CADENCE_MS;
}

/**
 * The centre of the window `atMs` falls in, counted from `originMs`.
 *
 * Averaging alone does not stop the flicker: a window that slides a frame at a
 * time still yields a slightly different mean on every frame, and it is the last
 * digit changing that the eye chases. Snapping the moment to the window it lands
 * in makes consecutive readings whole buckets apart, so the number holds still
 * for the length of one and then changes once — and everything else read at the
 * same moment, the clock included, holds still with it.
 *
 * The centre rather than the edge because the window is centred on the moment:
 * a reading stamped 14:43 that averages the two minutes after it is a lie the
 * size of the window.
 */
export function quantiseReadoutMs(atMs: number, originMs: number, windowMs: number): number {
  if (windowMs <= 0) return atMs;
  const bucket = Math.floor((atMs - originMs) / windowMs);
  return originMs + bucket * windowMs + windowMs / 2;
}

/** Weather summaries are sparse — hourly at worst — so the reach is wider. */
const WEATHER_GAP_MS = 90 * 60 * 1000;

export interface LiveSample {
  speed: number | null;
  heel: number | null;
}

export interface LiveWeatherSample {
  windSpeed: number | null;
  gust: number | null;
  directionDeg: number | null;
  temperature: number | null;
  tide: number | null;
}

const EMPTY_SAMPLE: LiveSample = { speed: null, heel: null };

const EMPTY_WEATHER: LiveWeatherSample = {
  windSpeed: null,
  gust: null,
  directionDeg: null,
  temperature: null,
  tide: null,
};

function numberOrNull(value: unknown): number | null {
  return typeof value === "number" && !Number.isNaN(value) ? value : null;
}

/** NaN is how the flat arrays below say what `numberOrNull` says with null. */
function orNull(value: number): number | null {
  return Number.isNaN(value) ? null : value;
}

/**
 * A day's fixes as flat arrays, with the timestamps parsed once.
 *
 * Everything here reads the fixes at the playhead, five times a second while a
 * replay runs, and `new Date(...).getTime()` over every fix of a ten-hour day
 * was most of the cost of doing that. Cached against the array itself: the fixes
 * are replaced wholesale when the journey loads and never edited in place, so
 * identity is a safe key and the cache dies with them.
 */
interface FixIndex {
  /** Milliseconds, in the order the fixes came in. NaN if one would not parse. */
  ms: Float64Array;
  speed: Float64Array;
  heel: Float64Array;
}

const fixIndexes = new WeakMap<GeoJSONFeature[], FixIndex>();

function fixIndex(features: GeoJSONFeature[]): FixIndex {
  const cached = fixIndexes.get(features);
  if (cached) return cached;

  const ms = new Float64Array(features.length);
  const speed = new Float64Array(features.length);
  const heel = new Float64Array(features.length);
  for (let i = 0; i < features.length; i++) {
    const properties = features[i].properties;
    ms[i] = new Date(properties.timestamp).getTime();
    speed[i] = numberOrNull(properties.speed) ?? NaN;
    heel[i] = numberOrNull(properties.heel) ?? NaN;
  }

  const index: FixIndex = { ms, speed, heel };
  fixIndexes.set(features, index);
  return index;
}

/** The fix nearest a moment, by index. */
function nearestFixIndex(ms: Float64Array, atMs: number): number {
  const lo = lowerBound(ms, atMs);
  // `lo` is the first fix at or after the moment; the one before it may be nearer.
  if (lo > 0 && (lo === ms.length || Math.abs(ms[lo - 1] - atMs) <= Math.abs(ms[lo] - atMs))) {
    return lo - 1;
  }
  return lo;
}

/** The first fix at or after a moment. The insertion point, not the nearest. */
function lowerBound(ms: Float64Array, atMs: number): number {
  let lo = 0;
  let hi = ms.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (ms[mid] < atMs) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/** The first fix strictly after a moment. */
function upperBound(ms: Float64Array, atMs: number): number {
  let lo = 0;
  let hi = ms.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (ms[mid] <= atMs) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

export interface SampleOptions {
  /**
   * Average every fix within this much journey time, centred on the moment,
   * rather than reading the one nearest it. `readoutWindowMs` sizes it from the
   * replay multiplier; zero — the default — is the single-fix reading.
   */
  windowMs?: number;
  toleranceMs?: number;
}

/**
 * The boat's own instruments at a moment: the nearest fix, so long as one was
 * recorded close enough to speak for it — or, given a window, the mean of every
 * fix inside it.
 *
 * The mean is what makes a fast replay readable. Both figures here are noisy at
 * the fix — a GPS speed jitters a tenth or two from one second to the next, and
 * heel swings with every wave — so at 300x the panel shows the scatter and not
 * the sailing. Averaging a window one viewer-second wide shows what the boat was
 * doing over the stretch the eye is being given to read it in.
 *
 * A window with no fix in it falls back to the nearest-fix reading rather than
 * to nothing, so the gap tolerance stays the one thing that decides whether the
 * playhead is standing in a recording silence.
 */
export function sampleAtMs(
  features: GeoJSONFeature[],
  atMs: number | null,
  options: SampleOptions = {},
): LiveSample {
  if (atMs === null || !features.length) return EMPTY_SAMPLE;
  const { windowMs = 0, toleranceMs = FIX_TOLERANCE_MS } = options;
  const index = fixIndex(features);

  if (windowMs > 0) {
    const mean = meanOverWindow(index, atMs, windowMs);
    if (mean !== null) return mean;
  }

  const i = nearestFixIndex(index.ms, atMs);
  const ms = index.ms[i];
  if (Number.isNaN(ms) || Math.abs(ms - atMs) > toleranceMs) return EMPTY_SAMPLE;

  return { speed: orNull(index.speed[i]), heel: orNull(index.heel[i]) };
}

/** Each instrument's mean over a centred window, or null if it holds no fix. */
function meanOverWindow(
  index: FixIndex,
  atMs: number,
  windowMs: number,
): LiveSample | null {
  let speedSum = 0;
  let speedCount = 0;
  let heelSum = 0;
  let heelCount = 0;
  let any = false;

  for (let i = lowerBound(index.ms, atMs - windowMs / 2); i < index.ms.length; i++) {
    const ms = index.ms[i];
    if (Number.isNaN(ms)) continue;
    if (ms > atMs + windowMs / 2) break;
    any = true;
    const speed = index.speed[i];
    if (!Number.isNaN(speed)) {
      speedSum += speed;
      speedCount++;
    }
    const heel = index.heel[i];
    if (!Number.isNaN(heel)) {
      heelSum += heel;
      heelCount++;
    }
  }

  if (!any) return null;
  return {
    speed: speedCount ? speedSum / speedCount : null,
    heel: heelCount ? heelSum / heelCount : null,
  };
}

/**
 * The running maximum speed from one moment onward, so a peak at the playhead is
 * a lookup rather than a walk.
 *
 * One stretch is cached per day of fixes, because the stretch is the scope and
 * the scope holds still for as long as the replay runs — which is exactly when
 * the peak is asked for five times a second. Rebuilding on a change of scope
 * costs the one walk the old code did on every reading.
 */
interface PeakIndex {
  fromMs: number;
  /** First fix at or after `fromMs`, where `prefix` starts counting. */
  startIdx: number;
  /** The most any fix from `startIdx` to here read; NaN until one of them did. */
  prefix: Float64Array;
}

const peakIndexes = new WeakMap<GeoJSONFeature[], PeakIndex>();

function peakIndex(features: GeoJSONFeature[], index: FixIndex, fromMs: number): PeakIndex {
  const cached = peakIndexes.get(features);
  if (cached && cached.fromMs === fromMs) return cached;

  const startIdx = lowerBound(index.ms, fromMs);
  const prefix = new Float64Array(index.ms.length - startIdx);
  let running = NaN;
  for (let i = startIdx; i < index.ms.length; i++) {
    const speed = index.speed[i];
    if (!Number.isNaN(speed) && (Number.isNaN(running) || speed > running)) running = speed;
    prefix[i - startIdx] = running;
  }

  const peak: PeakIndex = { fromMs, startIdx, prefix };
  peakIndexes.set(features, peak);
  return peak;
}

/**
 * The fastest the boat had been going by a moment, over a stretch — the mark the
 * speed dial carries above its bar.
 *
 * Derived rather than remembered. A peak needle held in a ref only ever rises,
 * which is what a peak needle should do, but it then also survives scrubbing
 * back, changing the scope and re-opening the journey — leaving the mark sitting
 * at a speed nothing on screen has reached, with nothing to say where it came
 * from. Computed to the playhead it climbs through a replay exactly as a held
 * one would, and rewinds when you do.
 */
export function peakSpeedTo(
  features: GeoJSONFeature[],
  fromMs: number,
  atMs: number | null,
): number | null {
  if (atMs === null || !features.length) return null;

  const index = fixIndex(features);
  const { startIdx, prefix } = peakIndex(features, index, fromMs);
  const end = upperBound(index.ms, atMs) - 1;
  if (end < startIdx) return null;
  return orNull(prefix[end - startIdx]);
}

interface Reading {
  ms: number;
  value: number;
}

/** One metric's readings, in order, with the station's misses dropped. */
function readings(
  series: readonly WeatherSummarySeriesPoint[],
  key: "wind_speed" | "gust" | "wind_direction" | "temperature" | "tide",
): Reading[] {
  const out: Reading[] = [];
  for (const point of series) {
    const value = numberOrNull(point[key]);
    if (value === null) continue;
    const ms = new Date(point.t).getTime();
    if (Number.isNaN(ms)) continue;
    out.push({ ms, value });
  }
  out.sort((a, b) => a.ms - b.ms);
  return out;
}

/**
 * Between the two readings either side, or nothing if the nearer of them is
 * further off than `maxGapMs`. Outside the series it holds the end value for
 * that same reach and then gives up — the station stopping is not the weather
 * staying still.
 */
function interpolate(list: Reading[], atMs: number, maxGapMs: number): number | null {
  if (!list.length) return null;

  const first = list[0];
  if (atMs <= first.ms) return atMs >= first.ms - maxGapMs ? first.value : null;

  const last = list[list.length - 1];
  if (atMs >= last.ms) return atMs <= last.ms + maxGapMs ? last.value : null;

  for (let i = 1; i < list.length; i++) {
    const b = list[i];
    if (b.ms < atMs) continue;
    const a = list[i - 1];
    if (Math.min(atMs - a.ms, b.ms - atMs) > maxGapMs) return null;
    const t = (atMs - a.ms) / Math.max(b.ms - a.ms, 1);
    return a.value + (b.value - a.value) * t;
  }
  return null;
}

/**
 * The nearest reading, for a quantity interpolation would lie about. Wind
 * direction is the one: halfway between 350° and 10° is 0°, not 180°, and a
 * dial that swings the long way round through south every time the wind backs
 * across north is worse than one that steps.
 */
function nearest(list: Reading[], atMs: number, maxGapMs: number): number | null {
  let best: Reading | null = null;
  for (const reading of list) {
    if (best === null || Math.abs(reading.ms - atMs) < Math.abs(best.ms - atMs)) best = reading;
  }
  if (best === null || Math.abs(best.ms - atMs) > maxGapMs) return null;
  return best.value;
}

/**
 * The weather at a moment, read off the day's series the way the tide is.
 *
 * The station's series is the only wind there is. Fixes carry `wind_speed` and
 * `wind_direction` of their own, which look like masthead readings and are not:
 * they are this same shore station copied onto the fix. Reading them instead
 * would buy no independent measurement, only a second path to the same number.
 */
export function weatherAtMs(
  series: readonly WeatherSummarySeriesPoint[],
  atMs: number | null,
  maxGapMs: number = WEATHER_GAP_MS,
): LiveWeatherSample {
  if (atMs === null || !series.length) return EMPTY_WEATHER;

  return {
    windSpeed: interpolate(readings(series, "wind_speed"), atMs, maxGapMs),
    gust: interpolate(readings(series, "gust"), atMs, maxGapMs),
    directionDeg: nearest(readings(series, "wind_direction"), atMs, maxGapMs),
    temperature: interpolate(readings(series, "temperature"), atMs, maxGapMs),
    tide: interpolate(readings(series, "tide"), atMs, maxGapMs),
  };
}
