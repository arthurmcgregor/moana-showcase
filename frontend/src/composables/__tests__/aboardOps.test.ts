import { describe, it, expect } from "vitest";
import { planAdd, planRemove, computeCrewForRange, isSubset } from "../aboardOps";
import type { AboardRecord } from "@/api/web";

function makeRecord(
  id: number,
  person: number,
  started_at: string,
  ended_at: string | null,
): AboardRecord {
  return { id, vessel: 1, person, started_at, ended_at, notes: "" };
}

const T0 = "2024-04-01T08:00:00Z";
const T1 = "2024-04-01T09:00:00Z";
const T2 = "2024-04-01T10:00:00Z";
const T3 = "2024-04-01T11:00:00Z";
const T4 = "2024-04-01T12:00:00Z";

describe("planAdd", () => {
  it("creates a new record when the person has no existing records", () => {
    const ops = planAdd([], 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([
      { kind: "create", person: 7, started_at: T1, ended_at: T2 },
    ]);
  });

  it("returns no ops when an existing record already covers the target", () => {
    const existing = [makeRecord(1, 7, T0, T4)];
    const ops = planAdd(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([]);
  });

  it("extends a record that ends exactly when the target begins", () => {
    const existing = [makeRecord(1, 7, T0, T1)];
    const ops = planAdd(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([
      { kind: "update", id: 1, started_at: T0, ended_at: T2 },
    ]);
  });

  it("extends a record that begins exactly when the target ends", () => {
    const existing = [makeRecord(1, 7, T2, T3)];
    const ops = planAdd(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([
      { kind: "update", id: 1, started_at: T1, ended_at: T3 },
    ]);
  });

  it("bridges two adjacent records into one when the target fills the gap", () => {
    const existing = [makeRecord(1, 7, T0, T1), makeRecord(2, 7, T2, T3)];
    const ops = planAdd(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([
      { kind: "update", id: 1, started_at: T0, ended_at: T3 },
      { kind: "delete", id: 2 },
    ]);
  });

  it("ignores records belonging to a different person", () => {
    const existing = [makeRecord(1, 99, T0, T4)];
    const ops = planAdd(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([
      { kind: "create", person: 7, started_at: T1, ended_at: T2 },
    ]);
  });

  it("treats an ongoing existing record as covering anything that starts within it", () => {
    const existing = [makeRecord(1, 7, T0, null)];
    const ops = planAdd(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([]);
  });

  it("creates an ongoing record when the target has no end time and there are no existing records", () => {
    const ops = planAdd([], 7, { started_at: T1, ended_at: null });
    expect(ops).toEqual([
      { kind: "create", person: 7, started_at: T1, ended_at: null },
    ]);
  });
});

describe("planRemove", () => {
  it("deletes a record whose bounds exactly match the target", () => {
    const existing = [makeRecord(1, 7, T1, T2)];
    const ops = planRemove(existing, 7, { started_at: T1, ended_at: T2 });
    expect(ops).toEqual([{ kind: "delete", id: 1 }]);
  });

  it("splits a record when the target sits inside it", () => {
    const existing = [makeRecord(1, 7, T0, T4)];
    const ops = planRemove(existing, 7, { started_at: T2, ended_at: T3 });
    expect(ops).toEqual([
      { kind: "update", id: 1, started_at: T0, ended_at: T2 },
      { kind: "create", person: 7, started_at: T3, ended_at: T4 },
    ]);
  });

  it("truncates the start when the target overlaps the front of a record", () => {
    const existing = [makeRecord(1, 7, T1, T3)];
    const ops = planRemove(existing, 7, { started_at: T0, ended_at: T2 });
    expect(ops).toEqual([
      { kind: "update", id: 1, started_at: T2, ended_at: T3 },
    ]);
  });

  it("truncates the end when the target overlaps the back of a record", () => {
    const existing = [makeRecord(1, 7, T1, T3)];
    const ops = planRemove(existing, 7, { started_at: T2, ended_at: T4 });
    expect(ops).toEqual([
      { kind: "update", id: 1, started_at: T1, ended_at: T2 },
    ]);
  });

  it("returns no ops when nothing overlaps the target", () => {
    const existing = [makeRecord(1, 7, T0, T1)];
    const ops = planRemove(existing, 7, { started_at: T2, ended_at: T3 });
    expect(ops).toEqual([]);
  });
});

describe("computeCrewForRange", () => {
  it("includes records that overlap the range and excludes those that don't", () => {
    const r1 = makeRecord(1, 7, T0, T2);
    const r2 = makeRecord(2, 8, T1, T3);
    const r3 = makeRecord(3, 9, T0, T4);
    const r4 = makeRecord(4, 10, T3, T4);
    const result = computeCrewForRange([r1, r2, r3, r4], { started_at: T1, ended_at: T2 });
    expect(result.map((r) => r.id).sort()).toEqual([1, 2, 3]);
  });

  it("treats an ongoing record as extending forever", () => {
    const r = makeRecord(1, 7, T0, null);
    const result = computeCrewForRange([r], { started_at: T3, ended_at: T4 });
    expect(result).toEqual([r]);
  });
});

describe("isSubset", () => {
  it("returns true for equal id sets", () => {
    expect(isSubset([1, 2, 3], [1, 2, 3])).toBe(true);
  });

  it("returns true for a strict subset", () => {
    expect(isSubset([1, 2], [1, 2, 3])).toBe(true);
  });

  it("returns false when the leg has someone the journey does not", () => {
    expect(isSubset([1, 4], [1, 2, 3])).toBe(false);
  });

  it("returns true for an empty leg crew", () => {
    expect(isSubset([], [1, 2, 3])).toBe(true);
  });
});
