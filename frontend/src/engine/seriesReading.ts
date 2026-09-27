import type { WeatherSummarySeriesPoint } from "@/api/web";

/**
 * One weather metric read at a moment, and which way it was going.
 *
 * Neither the tide nor the temperature has a useful average: the tide's only
 * interesting question is which way it is running, and a leg's mean temperature
 * sits halfway between its min and its max by construction. Both are therefore
 * a value and an arrow rather than a range — and, since the tide's own curve is
 * the most predictable shape on the screen, not a graph either.
 *
 * Lives here rather than in the component so it can be tested in the node env.
 */

/** How far either side of a moment the trend is measured over. */
const TREND_HALF_WINDOW_MS = 45 * 60 * 1000;

/** The numeric readings of a weather summary — the ones with a value to read. */
export type SeriesMetric = "tide" | "temperature" | "wind_speed" | "gust" | "humidity" | "rain";

export interface SeriesSample {
  ms: number;
  value: number;
}

/**
 * Which way it was going. `steady` is the metric's own name for too-slow-to-say
 * — slack water for the tide, a temperature holding still — so the caller
 * supplies the word and the threshold it is measured against.
 */
export type SeriesTrend = "rising" | "falling" | "steady";

/** The readings of one metric in a series, in order, with the misses dropped. */
export function seriesSamples(
  series: readonly WeatherSummarySeriesPoint[],
  metric: SeriesMetric,
): SeriesSample[] {
  const samples: SeriesSample[] = [];
  for (const point of series) {
    const value = point[metric];
    if (typeof value !== "number" || Number.isNaN(value)) continue;
    const ms = new Date(point.t).getTime();
    if (Number.isNaN(ms)) continue;
    samples.push({ ms, value });
  }
  samples.sort((a, b) => a.ms - b.ms);
  return samples;
}

/** Whether a metric was measured at all — what decides if it gets a readout. */
export function hasSeries(
  series: readonly WeatherSummarySeriesPoint[],
  metric: SeriesMetric,
): boolean {
  return series.some((p) => typeof p[metric] === "number" && !Number.isNaN(p[metric] as number));
}

/**
 * The value at a moment, interpolated between the readings either side of it and
 * clamped to the ends of the series.
 */
export function seriesValueAt(
  series: readonly WeatherSummarySeriesPoint[],
  metric: SeriesMetric,
  atMs: number,
): number | null {
  const samples = seriesSamples(series, metric);
  if (!samples.length) return null;
  if (atMs <= samples[0].ms) return samples[0].value;
  const last = samples[samples.length - 1];
  if (atMs >= last.ms) return last.value;

  for (let i = 1; i < samples.length; i++) {
    const b = samples[i];
    if (b.ms < atMs) continue;
    const a = samples[i - 1];
    const t = (atMs - a.ms) / Math.max(b.ms - a.ms, 1);
    return a.value + (b.value - a.value) * t;
  }
  return last.value;
}

/**
 * Which way it was going. The slope of a least-squares fit over the readings
 * around the moment rather than a first-to-last delta: near high water the
 * difference between two readings is smaller than the noise in them, and would
 * have the tide flipping direction every few minutes.
 *
 * `flatPerHour` is in the metric's own units per hour — metres for the tide,
 * degrees for temperature — because what counts as "not moving" is a fact about
 * the quantity, not about the fit.
 */
export function seriesTrend(
  series: readonly WeatherSummarySeriesPoint[],
  metric: SeriesMetric,
  atMs: number,
  flatPerHour: number,
  halfWindowMs: number = TREND_HALF_WINDOW_MS,
): SeriesTrend | null {
  const samples = seriesSamples(series, metric).filter(
    (s) => Math.abs(s.ms - atMs) <= halfWindowMs,
  );
  if (samples.length < 2) return null;

  // Hours from the moment, so the slope comes out in units per hour.
  const xs = samples.map((s) => (s.ms - atMs) / 3_600_000);
  const ys = samples.map((s) => s.value);
  const n = samples.length;
  const meanX = xs.reduce((a, b) => a + b, 0) / n;
  const meanY = ys.reduce((a, b) => a + b, 0) / n;

  let numerator = 0;
  let denominator = 0;
  for (let i = 0; i < n; i++) {
    numerator += (xs[i] - meanX) * (ys[i] - meanY);
    denominator += (xs[i] - meanX) ** 2;
  }
  if (denominator < 1e-12) return null;

  const slope = numerator / denominator;
  if (Math.abs(slope) < flatPerHour) return "steady";
  return slope > 0 ? "rising" : "falling";
}
