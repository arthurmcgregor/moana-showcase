import { describe, expect, it } from "vitest";
import type { WeatherSummarySeriesPoint } from "@/api/web";
import { hasSeries, seriesTrend, seriesValueAt } from "../seriesReading";

/** Metres an hour below which the water is turning rather than running. */
const TIDE_FLAT = 0.05;

const BASE = Date.parse("2026-07-01T00:00:00Z");

function point(minutes: number, tide: number | null): WeatherSummarySeriesPoint {
  return {
    t: new Date(BASE + minutes * 60_000).toISOString(),
    wind_speed: null,
    gust: null,
    wind_direction: null,
    temperature: null,
    humidity: null,
    rain: null,
    tide,
  };
}

/** A reading every ten minutes, rising by a tenth of a metre each time. */
function flood(count: number, fromMinute = 0): WeatherSummarySeriesPoint[] {
  return Array.from({ length: count }, (_, i) => point(fromMinute + i * 10, 1 + i * 0.1));
}


describe("seriesValueAt", () => {
  const series = [point(0, 1), point(60, 2)];

  it("interpolates between the readings either side", () => {
    expect(seriesValueAt(series, "tide", BASE + 30 * 60_000)).toBeCloseTo(1.5);
  });

  it("clamps to the ends of the series", () => {
    expect(seriesValueAt(series, "tide", BASE - 3_600_000)).toBeCloseTo(1);
    expect(seriesValueAt(series, "tide", BASE + 5 * 3_600_000)).toBeCloseTo(2);
  });

  it("has nothing to say without readings", () => {
    expect(seriesValueAt([], "tide", BASE)).toBeNull();
  });
});

describe("seriesTrend", () => {
  it("reads a flood as rising and an ebb as falling", () => {
    const rising = flood(13);
    const falling = rising.map((p, i) => ({ ...p, tide: rising[rising.length - 1 - i].tide }));

    expect(seriesTrend(rising, "tide", BASE + 60 * 60_000, TIDE_FLAT)).toBe("rising");
    expect(seriesTrend(falling, "tide", BASE + 60 * 60_000, TIDE_FLAT)).toBe("falling");
  });

  it("reads a barely-moving tide as slack", () => {
    const turning = Array.from({ length: 13 }, (_, i) => point(i * 10, 2 + i * 0.001));

    expect(seriesTrend(turning, "tide", BASE + 60 * 60_000, TIDE_FLAT)).toBe("steady");
  });

  it("ignores readings outside the window around the moment", () => {
    // A flood four hours earlier says nothing about the tide now.
    expect(seriesTrend(flood(13), "tide", BASE + 6 * 3_600_000, TIDE_FLAT)).toBeNull();
  });

  it("has nothing to say from a single reading", () => {
    expect(seriesTrend([point(0, 1.1)], "tide", BASE, TIDE_FLAT)).toBeNull();
  });
});

describe("across metrics", () => {
  function warming(count: number): WeatherSummarySeriesPoint[] {
    return Array.from({ length: count }, (_, i) => ({
      ...point(i * 10, 1 + i * 0.1),
      temperature: 14 + i * 0.5,
    }));
  }

  it("reads whichever metric it is asked for, from the same series", () => {
    expect(seriesValueAt(warming(5), "temperature", BASE + 20 * 60_000)).toBeCloseTo(15);
    // The same series, a different question: the tide climbed a tenth at a time.
    expect(seriesValueAt(warming(5), "tide", BASE + 20 * 60_000)).toBeCloseTo(1.2);
  });

  it("measures the trend in the metric's own units an hour", () => {
    // Half a degree every ten minutes is three an hour: steady only to a
    // threshold coarser than that.
    expect(seriesTrend(warming(7), "temperature", BASE + 30 * 60_000, 0.2)).toBe("rising");
    expect(seriesTrend(warming(7), "temperature", BASE + 30 * 60_000, 5)).toBe("steady");
  });

  it("knows whether a station reported a metric at all", () => {
    expect(hasSeries(warming(3), "temperature")).toBe(true);
    expect(hasSeries(flood(3), "temperature")).toBe(false);
    expect(hasSeries([], "tide")).toBe(false);
  });
});
