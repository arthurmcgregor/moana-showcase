import { describe, it, expect } from "vitest";
import { medianIntervalMs, gapThresholdMs, findGaps, splitAtGaps } from "../sampleGaps";

/** A run of samples `stepS` seconds apart, starting at `fromS`. */
function series(fromS: number, count: number, stepS: number): number[] {
  return Array.from({ length: count }, (_, i) => (fromS + i * stepS) * 1000);
}

describe("medianIntervalMs", () => {
  it("reports the usual interval", () => {
    expect(medianIntervalMs(series(0, 10, 1))).toBe(1000);
  });

  it("is not dragged by one long dropout", () => {
    const withDropout = [...series(0, 10, 1), 700_000];
    expect(medianIntervalMs(withDropout)).toBe(1000);
  });

  it("has no interval to report below two samples", () => {
    expect(medianIntervalMs([])).toBe(0);
    expect(medianIntervalMs([1000])).toBe(0);
  });
});

describe("gapThresholdMs", () => {
  it("floors at half a minute for a fast cadence", () => {
    // Ten seconds would shred a one-second trace on every ordinary hiccup.
    expect(gapThresholdMs(series(0, 100, 1))).toBe(30_000);
  });

  it("scales with a slow cadence", () => {
    // A device logging every minute is not in trouble at 30s of silence.
    expect(gapThresholdMs(series(0, 100, 60))).toBe(600_000);
  });
});

describe("findGaps", () => {
  it("finds the silences and leaves the cadence alone", () => {
    const samples = [...series(0, 5, 1), 300_000, 301_000];
    expect(findGaps(samples, 30_000)).toEqual([{ startMs: 4000, endMs: 300_000 }]);
  });

  it("finds nothing in an unbroken run", () => {
    expect(findGaps(series(0, 50, 2), 30_000)).toEqual([]);
  });

  it("finds nothing in a series too short to have an interval", () => {
    expect(findGaps([5000], 30_000)).toEqual([]);
  });
});

describe("splitAtGaps", () => {
  const ms = (n: number) => n;

  it("breaks a series at each gap", () => {
    const points = [0, 1000, 2000, 300_000, 301_000, 900_000];
    const gaps = [
      { startMs: 2000, endMs: 300_000 },
      { startMs: 301_000, endMs: 900_000 },
    ];
    expect(splitAtGaps(points, ms, gaps)).toEqual([
      [0, 1000, 2000],
      [300_000, 301_000],
      [900_000],
    ]);
  });

  it("keeps an ungapped series whole", () => {
    expect(splitAtGaps([0, 1000, 2000], ms, [])).toEqual([[0, 1000, 2000]]);
  });

  it("has no runs without items", () => {
    expect(splitAtGaps([], ms, [{ startMs: 0, endMs: 1 }])).toEqual([]);
  });

  it("does not break on wide spacing that holds no gap", () => {
    // The case downsampling creates: drawn points minutes apart on a long
    // journey, with every sample between them present.
    const drawn = [0, 600_000, 1_200_000];
    expect(splitAtGaps(drawn, ms, [])).toEqual([drawn]);
  });

  it("ignores a gap that does not sit between two points", () => {
    // A gap before the first drawn point cannot break a run.
    const drawn = [500_000, 501_000];
    expect(splitAtGaps(drawn, ms, [{ startMs: 0, endMs: 400_000 }])).toEqual([drawn]);
  });
});
