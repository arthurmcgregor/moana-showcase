import { describe, expect, it } from "vitest";

import { parseTrackList } from "../device";

describe("parseTrackList", () => {
  it("parses the new object shape with sizes", () => {
    const result = parseTrackList({
      tracks: [
        { name: "/gps_20260105_075924.csv", size: 12345 },
        { name: "/gps_20260106_101010.csv", size: 0 },
      ],
    });

    expect(result).toEqual([
      { name: "/gps_20260105_075924.csv", size: 12345 },
      { name: "/gps_20260106_101010.csv", size: 0 },
    ]);
  });

  it("parses the legacy string shape with size null (backward compatible)", () => {
    const result = parseTrackList({
      tracks: ["/gps_20260105_075924.csv", "/gps_20260106_101010.csv"],
    });

    expect(result).toEqual([
      { name: "/gps_20260105_075924.csv", size: null },
      { name: "/gps_20260106_101010.csv", size: null },
    ]);
  });

  it("defaults size to null when the object omits or mistypes size", () => {
    const result = parseTrackList({
      tracks: [{ name: "/a.csv" }, { name: "/b.csv", size: "nope" }],
    });

    expect(result).toEqual([
      { name: "/a.csv", size: null },
      { name: "/b.csv", size: null },
    ]);
  });

  it("returns an empty array for an empty list", () => {
    expect(parseTrackList({ tracks: [] })).toEqual([]);
  });

  it("throws when tracks is missing or not an array", () => {
    expect(() => parseTrackList({})).toThrow(/Invalid response format/);
    expect(() => parseTrackList(null)).toThrow(/Invalid response format/);
    expect(() => parseTrackList({ tracks: "x" })).toThrow(/Invalid response format/);
  });

  it("throws on an unrecognised track entry", () => {
    expect(() => parseTrackList({ tracks: [123] })).toThrow(/Invalid track entry/);
  });
});
