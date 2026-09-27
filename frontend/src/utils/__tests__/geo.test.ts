import { describe, it, expect } from "vitest";
import { distanceNm } from "../geo";

describe("distanceNm", () => {
  it("is zero for the same point", () => {
    expect(distanceNm([174.76, -36.85], [174.76, -36.85])).toBe(0);
  });

  it("makes one minute of latitude one nautical mile", () => {
    expect(distanceNm([174, -36], [174, -36 - 1 / 60])).toBeCloseTo(1, 2);
  });

  it("shortens a degree of longitude with latitude", () => {
    const atEquator = distanceNm([0, 0], [1, 0]);
    const atSixty = distanceNm([0, 60], [1, 60]);

    expect(atEquator).toBeCloseTo(60, 0);
    expect(atSixty).toBeCloseTo(30, 0);
  });

  it("is symmetric", () => {
    const a: [number, number] = [174.76, -36.85];
    const b: [number, number] = [174.9, -36.7];

    expect(distanceNm(a, b)).toBeCloseTo(distanceNm(b, a), 10);
  });

  it("matches a known great-circle distance", () => {
    // Auckland → Wellington, ~262 nm.
    const distance = distanceNm([174.7633, -36.8485], [174.7762, -41.2865]);
    expect(distance).toBeGreaterThan(258);
    expect(distance).toBeLessThan(268);
  });

  it("handles crossing the antimeridian the short way", () => {
    expect(distanceNm([179.9, 0], [-179.9, 0])).toBeCloseTo(12, 0);
  });
});
