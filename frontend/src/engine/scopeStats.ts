import type { Annotation, GeoJSONFeature, WeatherSummarySeriesPoint } from "@/api/web";
import type { TimeWindow } from "./scope";
import { isInWindow } from "./scope";
import { distanceNm } from "@/utils/geo";

/**
 * Stats for whatever the screen is scoped to, computed from the fixes inside
 * the window. Journey and leg distances come from the API, which measures on
 * the spheroid in PostGIS; everything else is computed here because there is no
 * per-segment endpoint and because a min–max speed range has to come from the
 * fixes either way — the API only carries an average and a maximum.
 */

export interface Range {
  min: number;
  max: number;
}

export interface SpeedStats extends Range {
  avg: number;
}

export interface ScopeStats {
  distanceNm: number;
  elapsedMs: number;
  fixCount: number;
  speed: SpeedStats | null;
  heel: Range | null;
  tacks: number;
  gybes: number;
}

function rangeOf(values: number[]): Range | null {
  if (!values.length) return null;
  let min = Infinity;
  let max = -Infinity;
  for (const v of values) {
    if (v < min) min = v;
    if (v > max) max = v;
  }
  return { min, max };
}

export function statsForWindow(
  features: GeoJSONFeature[],
  window: TimeWindow,
  annotations: Annotation[] = [],
): ScopeStats {
  const inWindow = features.filter((f) => isInWindow(f.properties.timestamp, window));

  let distance = 0;
  for (let i = 1; i < inWindow.length; i++) {
    distance += distanceNm(
      inWindow[i - 1].geometry.coordinates,
      inWindow[i].geometry.coordinates,
    );
  }

  // The window is the truth about elapsed time; the fixes only stand in for it
  // while the scope is still running and has no end.
  let elapsedMs = 0;
  if (Number.isFinite(window.endMs)) {
    elapsedMs = Math.max(0, window.endMs - window.startMs);
  } else if (inWindow.length > 1) {
    const first = new Date(inWindow[0].properties.timestamp).getTime();
    const last = new Date(inWindow[inWindow.length - 1].properties.timestamp).getTime();
    elapsedMs = Math.max(0, last - first);
  }

  const speeds = inWindow
    .map((f) => f.properties.speed)
    .filter((s): s is number => typeof s === "number" && !Number.isNaN(s));
  const speedRange = rangeOf(speeds);
  const speed: SpeedStats | null = speedRange
    ? { ...speedRange, avg: speeds.reduce((a, b) => a + b, 0) / speeds.length }
    : null;

  const heels = inWindow
    .map((f) => f.properties.heel)
    .filter((h): h is number => typeof h === "number" && !Number.isNaN(h));

  const scopedAnnotations = annotations.filter((a) => isInWindow(a.timestamp, window));

  return {
    distanceNm: distance,
    elapsedMs,
    fixCount: inWindow.length,
    speed,
    heel: rangeOf(heels),
    tacks: scopedAnnotations.filter((a) => a.type === "TACK").length,
    gybes: scopedAnnotations.filter((a) => a.type === "GYBE").length,
  };
}

// ── Weather ──────────────────────────────────────────────────────────────────

export interface WeatherWindowStats {
  wind: { avg: number; max: number; directionDeg: number | null } | null;
  gustMax: number | null;
  temperature: (Range & { avg: number }) | null;
}

/**
 * Wind direction is a circular quantity: averaging 350° and 10° arithmetically
 * gives 180°, the opposite of the answer. Summing unit vectors gives 0°.
 */
function meanDirection(degrees: number[]): number | null {
  if (!degrees.length) return null;
  let x = 0;
  let y = 0;
  for (const deg of degrees) {
    const rad = (deg * Math.PI) / 180;
    x += Math.cos(rad);
    y += Math.sin(rad);
  }
  if (Math.abs(x) < 1e-9 && Math.abs(y) < 1e-9) return null;
  return ((Math.atan2(y, x) * 180) / Math.PI + 360) % 360;
}

function numbers(points: WeatherSummarySeriesPoint[], key: keyof WeatherSummarySeriesPoint): number[] {
  return points
    .map((p) => p[key])
    .filter((v): v is number => typeof v === "number" && !Number.isNaN(v));
}

export function weatherForWindow(
  series: WeatherSummarySeriesPoint[],
  window: TimeWindow,
): WeatherWindowStats {
  const points = series.filter((p) => isInWindow(p.t, window));

  const windSpeeds = numbers(points, "wind_speed");
  const windRange = rangeOf(windSpeeds);
  const temps = numbers(points, "temperature");
  const tempRange = rangeOf(temps);
  const gusts = numbers(points, "gust");

  return {
    wind: windRange
      ? {
          avg: windSpeeds.reduce((a, b) => a + b, 0) / windSpeeds.length,
          max: windRange.max,
          directionDeg: meanDirection(numbers(points, "wind_direction")),
        }
      : null,
    gustMax: gusts.length ? Math.max(...gusts) : null,
    temperature: tempRange
      ? { ...tempRange, avg: temps.reduce((a, b) => a + b, 0) / temps.length }
      : null,
  };
}
