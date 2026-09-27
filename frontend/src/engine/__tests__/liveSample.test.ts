import { describe, expect, it } from "vitest";
import type { GeoJSONFeature, WeatherSummarySeriesPoint } from "@/api/web";
import {
  READOUT_CADENCE_MS,
  peakSpeedTo,
  quantiseReadoutMs,
  readoutWindowMs,
  sampleAtMs,
  weatherAtMs,
} from "../liveSample";

const BASE = Date.parse("2026-07-01T00:00:00Z");

function fix(seconds: number, speed: number | undefined, heel?: number | null): GeoJSONFeature {
  return {
    type: "Feature",
    geometry: { type: "Point", coordinates: [174.8, -41.3] },
    properties: {
      timestamp: new Date(BASE + seconds * 1000).toISOString(),
      speed,
      heel,
      track_id: 1,
    },
  };
}

function reading(
  minutes: number,
  values: Partial<WeatherSummarySeriesPoint> = {},
): WeatherSummarySeriesPoint {
  return {
    t: new Date(BASE + minutes * 60_000).toISOString(),
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

describe("sampleAtMs", () => {
  const fixes = [fix(0, 3, 4), fix(10, 5, 8), fix(20, 7, 12)];

  it("reads nothing without a moment or without fixes", () => {
    expect(sampleAtMs(fixes, null)).toEqual({ speed: null, heel: null });
    expect(sampleAtMs([], BASE)).toEqual({ speed: null, heel: null });
  });

  it("takes the nearest fix either side of the moment", () => {
    expect(sampleAtMs(fixes, BASE + 9_000).speed).toBe(5);
    expect(sampleAtMs(fixes, BASE + 11_000).speed).toBe(5);
    expect(sampleAtMs(fixes, BASE + 16_000)).toEqual({ speed: 7, heel: 12 });
  });

  it("holds the ends of the recording", () => {
    expect(sampleAtMs(fixes, BASE - 5_000).speed).toBe(3);
    expect(sampleAtMs(fixes, BASE + 25_000).speed).toBe(7);
  });

  it("reads nothing when the nearest fix is beyond the tolerance", () => {
    // A playhead parked in a recording gap borrows no number from either side.
    expect(sampleAtMs(fixes, BASE + 5 * 60_000)).toEqual({ speed: null, heel: null });
  });

  it("reports a missing instrument as missing, not as zero", () => {
    expect(sampleAtMs([fix(0, undefined, null)], BASE)).toEqual({ speed: null, heel: null });
  });
});

describe("sampleAtMs over a window", () => {
  const fixes = [fix(0, 2, 0), fix(10, 4, 10), fix(20, 6, 20), fix(60, 100, 100)];

  it("averages every fix inside the window and no fix outside it", () => {
    // 30s either side of the ten-second mark takes the first three and leaves
    // the outlier at 60s alone.
    const at = sampleAtMs(fixes, BASE + 10_000, { windowMs: 60_000 });

    expect(at.speed).toBeCloseTo(4);
    expect(at.heel).toBeCloseTo(10);
  });

  it("narrows to one fix as the window narrows", () => {
    expect(sampleAtMs(fixes, BASE + 20_000, { windowMs: 4_000 }).speed).toBe(6);
  });

  it("falls back to the nearest fix when the window holds none", () => {
    // Between the run and the outlier: an empty window is not a recording gap,
    // and the tolerance is what decides that.
    expect(sampleAtMs(fixes, BASE + 40_000, { windowMs: 2_000 }).speed).toBe(6);
    expect(sampleAtMs(fixes, BASE + 20 * 60_000, { windowMs: 2_000 }).speed).toBeNull();
  });

  it("averages the instruments independently of each other", () => {
    // A fix that carried a speed and no heel must not drag the heel average
    // down, nor count against it.
    const partial = [fix(0, 4, 8), fix(10, 6, null)];

    expect(sampleAtMs(partial, BASE + 5_000, { windowMs: 60_000 })).toEqual({
      speed: 5,
      heel: 8,
    });
  });
});

describe("readoutWindowMs", () => {
  it("is one cadence of wall clock, in journey time", () => {
    expect(readoutWindowMs(1)).toBe(READOUT_CADENCE_MS);
    expect(readoutWindowMs(300)).toBe(300 * READOUT_CADENCE_MS);
  });

  it("is nothing while the playhead is not running", () => {
    expect(readoutWindowMs(0)).toBe(0);
    expect(readoutWindowMs(-5)).toBe(0);
  });
});

describe("quantiseReadoutMs", () => {
  it("gives back the moment untouched without a window", () => {
    expect(quantiseReadoutMs(BASE + 1234, BASE, 0)).toBe(BASE + 1234);
  });

  it("lands on the centre of the window the moment falls in", () => {
    expect(quantiseReadoutMs(BASE + 0, BASE, 1_000)).toBe(BASE + 500);
    expect(quantiseReadoutMs(BASE + 999, BASE, 1_000)).toBe(BASE + 500);
    expect(quantiseReadoutMs(BASE + 1_000, BASE, 1_000)).toBe(BASE + 1_500);
  });

  it("holds still across a whole window, which is the point of it", () => {
    const steps = [0, 120, 300, 899].map((ms) => quantiseReadoutMs(BASE + ms, BASE, 900));

    expect(new Set(steps).size).toBe(1);
  });

  it("steps backwards the same way before the origin", () => {
    expect(quantiseReadoutMs(BASE - 1, BASE, 1_000)).toBe(BASE - 500);
  });
});

describe("peakSpeedTo", () => {
  const fixes = [fix(0, 3), fix(10, 9), fix(20, 5), fix(30, 12)];

  it("reads nothing without a moment or without fixes", () => {
    expect(peakSpeedTo(fixes, BASE, null)).toBeNull();
    expect(peakSpeedTo([], BASE, BASE)).toBeNull();
  });

  it("rises to the fastest fix reached and no further", () => {
    expect(peakSpeedTo(fixes, BASE, BASE + 5_000)).toBe(3);
    expect(peakSpeedTo(fixes, BASE, BASE + 15_000)).toBe(9);
    // Slowing down does not lower it.
    expect(peakSpeedTo(fixes, BASE, BASE + 25_000)).toBe(9);
    expect(peakSpeedTo(fixes, BASE, BASE + 60_000)).toBe(12);
  });

  it("counts only from the start of the stretch it is given", () => {
    // A scoped leg's mark is that leg's best, not the day's.
    expect(peakSpeedTo(fixes, BASE + 15_000, BASE + 25_000)).toBe(5);
  });

  it("reads nothing before the stretch has a fix in it", () => {
    expect(peakSpeedTo(fixes, BASE + 15_000, BASE + 16_000)).toBeNull();
  });

  it("ignores fixes that carried no speed", () => {
    expect(peakSpeedTo([fix(0, undefined), fix(10, 4)], BASE, BASE + 20_000)).toBe(4);
  });

  it("answers for whichever stretch it was last asked about", () => {
    // One stretch is cached per array of fixes, so scoping a leg, scoping the
    // day and scoping the leg again has to rebuild rather than answer from the
    // stretch before it.
    expect(peakSpeedTo(fixes, BASE + 15_000, BASE + 25_000)).toBe(5);
    expect(peakSpeedTo(fixes, BASE, BASE + 25_000)).toBe(9);
    expect(peakSpeedTo(fixes, BASE + 15_000, BASE + 25_000)).toBe(5);
  });
});

describe("weatherAtMs", () => {
  const series = [
    reading(0, { wind_speed: 6, gust: 10, wind_direction: 200, temperature: 15 }),
    reading(60, { wind_speed: 10, gust: 16, wind_direction: 240, temperature: 17 }),
  ];

  it("reads nothing without a moment or without readings", () => {
    expect(weatherAtMs(series, null).windSpeed).toBeNull();
    expect(weatherAtMs([], BASE).windSpeed).toBeNull();
  });

  it("interpolates between the readings either side", () => {
    const at = weatherAtMs(series, BASE + 30 * 60_000);

    expect(at.windSpeed).toBeCloseTo(8);
    expect(at.gust).toBeCloseTo(13);
    expect(at.temperature).toBeCloseTo(16);
  });

  it("steps direction to the nearest reading rather than interpolating it", () => {
    // Halfway between 350° and 10° is 0°, and no linear blend of the two says so.
    const backing = [
      reading(0, { wind_direction: 350 }),
      reading(60, { wind_direction: 10 }),
    ];

    expect(weatherAtMs(backing, BASE + 20 * 60_000).directionDeg).toBe(350);
    expect(weatherAtMs(backing, BASE + 40 * 60_000).directionDeg).toBe(10);
  });

  it("holds the last reading only as far as the gap allows", () => {
    expect(weatherAtMs(series, BASE + 120 * 60_000).windSpeed).toBe(10);
    expect(weatherAtMs(series, BASE + 200 * 60_000).windSpeed).toBeNull();
  });

  it("says nothing across a silence in the middle of the day", () => {
    const gapped = [
      reading(0, { wind_speed: 6 }),
      reading(600, { wind_speed: 20 }),
    ];

    expect(weatherAtMs(gapped, BASE + 300 * 60_000).windSpeed).toBeNull();
    // Close to a reading it still answers, from that reading's side of the gap.
    expect(weatherAtMs(gapped, BASE + 30 * 60_000).windSpeed).toBeCloseTo(6.7, 1);
  });

  it("reports a metric the station never sent as missing", () => {
    expect(weatherAtMs([reading(0, { wind_speed: 6 })], BASE).temperature).toBeNull();
  });
});
