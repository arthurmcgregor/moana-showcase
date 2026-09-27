import { describe, it, expect } from "vitest";
import { statsForWindow, weatherForWindow } from "../scopeStats";
import type { Annotation, GeoJSONFeature, WeatherSummarySeriesPoint } from "@/api/web";

function makeFeature(
  timestamp: string,
  lng: number,
  lat: number,
  properties: Partial<GeoJSONFeature["properties"]> = {},
): GeoJSONFeature {
  return {
    type: "Feature",
    geometry: { type: "Point", coordinates: [lng, lat] },
    properties: { timestamp, track_id: 1, ...properties },
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

const T0 = "2024-04-01T08:00:00Z";
const T1 = "2024-04-01T09:00:00Z";
const T2 = "2024-04-01T10:00:00Z";
const T3 = "2024-04-01T11:00:00Z";

const ms = (iso: string) => new Date(iso).getTime();
const HOUR = 60 * 60 * 1000;

describe("statsForWindow", () => {
  const features = [
    makeFeature(T0, 174.0, -36.0, { speed: 4, heel: -5 }),
    makeFeature(T1, 174.0, -36.1, { speed: 6, heel: 12 }),
    makeFeature(T2, 174.0, -36.2, { speed: 8, heel: 3 }),
    makeFeature(T3, 174.0, -36.3, { speed: 2, heel: -8 }),
  ];

  it("sums distance along the fixes in the window", () => {
    const stats = statsForWindow(features, { startMs: ms(T0), endMs: ms(T1) });

    // One tenth of a degree of latitude is six nautical miles.
    expect(stats.distanceNm).toBeCloseTo(6, 1);
    expect(stats.fixCount).toBe(2);
  });

  it("counts a boundary fix in both of the scopes that share it", () => {
    const first = statsForWindow(features, { startMs: ms(T0), endMs: ms(T1) });
    const second = statsForWindow(features, { startMs: ms(T1), endMs: ms(T2) });
    const both = statsForWindow(features, { startMs: ms(T0), endMs: ms(T2) });

    expect(first.distanceNm + second.distanceNm).toBeCloseTo(both.distanceNm, 6);
  });

  it("takes elapsed from the window, not the fixes", () => {
    const stats = statsForWindow(features, { startMs: ms(T0), endMs: ms(T2) });
    expect(stats.elapsedMs).toBe(2 * HOUR);
  });

  it("falls back to the fix span when the scope is still running", () => {
    const stats = statsForWindow(features, { startMs: ms(T0), endMs: Infinity });
    expect(stats.elapsedMs).toBe(3 * HOUR);
  });

  it("reports speed as an average with its range", () => {
    const stats = statsForWindow(features, { startMs: ms(T0), endMs: ms(T3) });

    expect(stats.speed).toEqual({ min: 2, max: 8, avg: 5 });
  });

  it("keeps the sign of heel in the range", () => {
    const stats = statsForWindow(features, { startMs: ms(T0), endMs: ms(T3) });
    expect(stats.heel).toEqual({ min: -8, max: 12 });
  });

  it("ignores fixes with no speed or heel rather than counting them as zero", () => {
    const sparse = [
      makeFeature(T0, 174, -36, { speed: 4 }),
      makeFeature(T1, 174, -36.1),
      makeFeature(T2, 174, -36.2, { speed: 6, heel: null }),
    ];
    const stats = statsForWindow(sparse, { startMs: ms(T0), endMs: ms(T2) });

    expect(stats.speed).toEqual({ min: 4, max: 6, avg: 5 });
    expect(stats.heel).toBeNull();
  });

  it("counts tacks and gybes inside the window only", () => {
    const annotations = [
      makeAnnotation(1, "TACK", T0),
      makeAnnotation(2, "TACK", T1),
      makeAnnotation(3, "GYBE", T1),
      makeAnnotation(4, "TACK", T3),
      makeAnnotation(5, "NOTE", T1),
    ];
    const stats = statsForWindow(features, { startMs: ms(T0), endMs: ms(T2) }, annotations);

    expect(stats.tacks).toBe(2);
    expect(stats.gybes).toBe(1);
  });

  it("survives an empty window", () => {
    const stats = statsForWindow([], { startMs: ms(T0), endMs: ms(T1) });

    expect(stats).toEqual({
      distanceNm: 0,
      elapsedMs: HOUR,
      fixCount: 0,
      speed: null,
      heel: null,
      tacks: 0,
      gybes: 0,
    });
  });
});

describe("weatherForWindow", () => {
  function point(
    t: string,
    values: Partial<Omit<WeatherSummarySeriesPoint, "t">> = {},
  ): WeatherSummarySeriesPoint {
    return {
      t,
      wind_speed: null,
      gust: null,
      wind_direction: null,
      temperature: null,
      humidity: null,
      rain: null,
      tide: null,
      ...values,
    };
  }

  const series = [
    point(T0, { wind_speed: 10, gust: 15, wind_direction: 350, temperature: 14, tide: 1.2 }),
    point(T1, { wind_speed: 14, gust: 22, wind_direction: 10, temperature: 18, tide: 2.0 }),
    point(T2, { wind_speed: 12, gust: 30, wind_direction: 0, temperature: 16, tide: 1.6 }),
  ];

  it("averages wind speed and takes the strongest gust in the window", () => {
    const weather = weatherForWindow(series, { startMs: ms(T0), endMs: ms(T1) });

    expect(weather.wind?.avg).toBe(12);
    expect(weather.wind?.max).toBe(14);
    expect(weather.gustMax).toBe(22);
  });

  it("averages wind direction around the compass, not through it", () => {
    const weather = weatherForWindow(series, { startMs: ms(T0), endMs: ms(T1) });

    // 350° and 10° average to 0°, never to 180°.
    expect(weather.wind?.directionDeg).toBeCloseTo(0, 6);
  });

  it("gives temperature as an average with its range", () => {
    const weather = weatherForWindow(series, { startMs: ms(T0), endMs: ms(T2) });

    expect(weather.temperature).toEqual({ min: 14, max: 18, avg: 16 });
  });

  it("returns nulls rather than zeros when the window has no observations", () => {
    const weather = weatherForWindow(series, { startMs: ms(T3), endMs: ms(T3) });

    expect(weather).toEqual({
      wind: null,
      gustMax: null,
      temperature: null,
    });
  });
});
