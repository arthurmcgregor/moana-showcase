import { describe, expect, it } from "vitest";
import { useTraceStore } from "../useTraceStore";

describe("useTraceStore", () => {
  it("starts empty and invalid", () => {
    const s = useTraceStore();
    expect(s.coordinates.value).toEqual([]);
    expect(s.isValid.value).toBe(false);
    expect(s.toLineString()).toBeNull();
  });

  it("addPoint appends coordinates", () => {
    const s = useTraceStore();
    s.addPoint([174.78, -41.29]);
    s.addPoint([174.82, -41.3]);
    expect(s.coordinates.value).toEqual([
      [174.78, -41.29],
      [174.82, -41.3],
    ]);
  });

  it("isValid flips true at two points", () => {
    const s = useTraceStore();
    s.addPoint([1, 1]);
    expect(s.isValid.value).toBe(false);
    s.addPoint([2, 2]);
    expect(s.isValid.value).toBe(true);
  });

  it("undo removes the last point (LIFO)", () => {
    const s = useTraceStore();
    s.addPoint([1, 1]);
    s.addPoint([2, 2]);
    s.addPoint([3, 3]);
    s.undo();
    expect(s.coordinates.value).toEqual([
      [1, 1],
      [2, 2],
    ]);
  });

  it("undo on empty is a no-op", () => {
    const s = useTraceStore();
    s.undo();
    expect(s.coordinates.value).toEqual([]);
  });

  it("clear empties the buffer", () => {
    const s = useTraceStore();
    s.addPoint([1, 1]);
    s.addPoint([2, 2]);
    s.clear();
    expect(s.coordinates.value).toEqual([]);
    expect(s.isValid.value).toBe(false);
  });

  it("toLineString returns a valid GeoJSON LineString when >= 2 points", () => {
    const s = useTraceStore();
    s.addPoint([174.78, -41.29]);
    s.addPoint([174.82, -41.3]);
    expect(s.toLineString()).toEqual({
      type: "LineString",
      coordinates: [
        [174.78, -41.29],
        [174.82, -41.3],
      ],
    });
  });

  it("toLineString returns null with 1 point", () => {
    const s = useTraceStore();
    s.addPoint([1, 1]);
    expect(s.toLineString()).toBeNull();
  });
});
