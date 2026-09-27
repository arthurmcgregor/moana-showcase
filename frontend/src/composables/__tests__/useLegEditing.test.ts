import { describe, it, expect, vi, beforeEach } from "vitest";
import type { Leg } from "@/api/web";
import type { TreeSegment } from "@/engine";

const invalidateQueries = vi.fn();
vi.mock("@pinia/colada", () => ({
  useQueryCache: () => ({ invalidateQueries }),
}));

const createLeg = vi.fn();
const updateLeg = vi.fn();
const deleteLeg = vi.fn();
vi.mock("@/api/web", () => ({
  createLeg: (...args: unknown[]) => createLeg(...args),
  updateLeg: (...args: unknown[]) => updateLeg(...args),
  deleteLeg: (...args: unknown[]) => deleteLeg(...args),
}));

const { useLegEditing } = await import("../useLegEditing");

// Six one-hour segments back to back, ids 1..6.
const T0 = Date.UTC(2024, 0, 1, 0, 0, 0);
const HOUR = 3_600_000;

function segmentAt(i: number): TreeSegment {
  const startMs = T0 + i * HOUR;
  const endMs = startMs + HOUR;
  return {
    kind: "mode",
    id: i + 1,
    mode: "SAIL",
    label: "Sailing",
    color: "#000",
    startMs,
    endMs,
    durationMs: HOUR,
    startedAt: new Date(startMs).toISOString(),
    endedAt: new Date(endMs).toISOString(),
    chips: [],
    comments: [],
  };
}

const SEGMENTS = Array.from({ length: 6 }, (_, i) => segmentAt(i));

/** A leg over segments `from`..`to` inclusive, by the same bounds the UI writes. */
function legOver(id: number, from: number, to: number): Leg {
  return {
    id,
    name: `Leg ${id}`,
    notes: "",
    started_at: SEGMENTS[from].startedAt,
    ended_at: SEGMENTS[to].endedAt,
  } as Leg;
}

function setup(legs: Leg[] = []) {
  const state = { legs };
  const editing = useLegEditing(
    () => SEGMENTS,
    () => state.legs,
    () => 7,
  );
  return { editing, state };
}

beforeEach(() => {
  vi.clearAllMocks();
});

describe("useLegEditing — membership", () => {
  it("opens a leg over the segments its stored range covers", () => {
    const { editing } = setup([legOver(1, 1, 3)]);

    editing.begin(legOver(1, 1, 3));

    expect([...editing.runIds.value]).toEqual([2, 3, 4]);
    expect(editing.name.value).toBe("Leg 1");
  });

  it("extends and retreats the run as segments are clicked", () => {
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));

    editing.toggleMembership(4);
    expect([...editing.runIds.value].sort()).toEqual([2, 3, 4]);

    editing.toggleMembership(4);
    expect([...editing.runIds.value].sort()).toEqual([2, 3]);
  });

  it("refuses a segment another leg owns", () => {
    const { editing } = setup([legOver(1, 0, 1), legOver(2, 4, 5)]);
    editing.begin(legOver(1, 0, 1));

    expect([...editing.claimedIds.value].sort()).toEqual([5, 6]);

    editing.toggleMembership(5);
    expect([...editing.runIds.value].sort()).toEqual([1, 2]);
  });

  it("does not count the leg being edited as claimed by anyone", () => {
    const { editing } = setup([legOver(1, 0, 1)]);
    editing.begin(legOver(1, 0, 1));

    expect(editing.claimedIds.value.size).toBe(0);
  });

  it("ignores a click with no leg open", () => {
    const { editing } = setup();

    editing.toggleMembership(3);

    expect(editing.runIds.value.size).toBe(0);
    expect(editing.override.value).toBeNull();
  });

  it("publishes the pending run as an override so the tree can preview it", () => {
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));
    editing.toggleMembership(4);

    expect(editing.override.value).toEqual({
      legId: 1,
      segmentIds: editing.runIds.value,
      bounds: { start: SEGMENTS[1].startedAt, end: SEGMENTS[3].endedAt },
    });
  });
});

describe("useLegEditing — writes", () => {
  it("saves the pending name, notes and bounds, then closes", async () => {
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));
    editing.name.value = "  Across the bay  ";
    editing.notes.value = "Choppy";
    editing.toggleMembership(4);

    expect(await editing.save()).toBe(true);

    expect(updateLeg).toHaveBeenCalledWith(1, {
      name: "Across the bay",
      notes: "Choppy",
      started_at: SEGMENTS[1].startedAt,
      ended_at: SEGMENTS[3].endedAt,
    });
    expect(editing.editingLegId.value).toBeNull();
    expect(invalidateQueries).toHaveBeenCalledTimes(2);
  });

  it("refuses to save a leg with no name or no segments", async () => {
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));

    editing.name.value = "   ";
    expect(await editing.save()).toBe(false);

    editing.name.value = "Fine";
    editing.toggleMembership(3);
    editing.toggleMembership(2);
    expect(editing.runIds.value.size).toBe(0);
    expect(await editing.save()).toBe(false);

    expect(updateLeg).not.toHaveBeenCalled();
  });

  it("keeps the edit open and reports why when the write fails", async () => {
    updateLeg.mockRejectedValueOnce(new Error("409 Conflict"));
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));
    editing.name.value = "Renamed";

    expect(await editing.save()).toBe(false);

    // The typing survives: a dropped connection must not throw the edit away.
    expect(editing.editingLegId.value).toBe(1);
    expect(editing.name.value).toBe("Renamed");
    expect(editing.error.value).toBe("409 Conflict");
    expect(editing.isSaving.value).toBe(false);
    expect(invalidateQueries).not.toHaveBeenCalled();
  });

  it("clears a stale error when the next write starts", async () => {
    updateLeg.mockRejectedValueOnce(new Error("nope"));
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));
    editing.name.value = "Renamed";
    await editing.save();

    expect(await editing.save()).toBe(true);
    expect(editing.error.value).toBeNull();
  });

  it("deletes the open leg and closes", async () => {
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));

    expect(await editing.remove()).toBe(true);

    expect(deleteLeg).toHaveBeenCalledWith(1);
    expect(editing.editingLegId.value).toBeNull();
  });

  it("keeps the edit open when the delete fails", async () => {
    deleteLeg.mockRejectedValueOnce(new Error("500"));
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));

    expect(await editing.remove()).toBe(false);

    expect(editing.editingLegId.value).toBe(1);
    expect(editing.error.value).toBe("500");
  });

  it("creates a leg spanning an arbitrary run", async () => {
    const { editing } = setup();

    expect(await editing.create("  Morning  ", [3, 4])).toBe(true);

    expect(createLeg).toHaveBeenCalledWith({
      journey: 7,
      name: "Morning",
      started_at: SEGMENTS[2].startedAt,
      ended_at: SEGMENTS[3].endedAt,
      notes: "",
    });
  });

  it("refuses to create over nothing, or with no name", async () => {
    const { editing } = setup();

    expect(await editing.create("Morning", [])).toBe(false);
    expect(await editing.create("  ", [3])).toBe(false);

    expect(createLeg).not.toHaveBeenCalled();
  });

  it("reports a failed create without closing the caller's selection", async () => {
    createLeg.mockRejectedValueOnce(new Error("bad request"));
    const { editing } = setup();

    expect(await editing.create("Morning", [3, 4])).toBe(false);
    expect(editing.error.value).toBe("bad request");
  });

  it("cancel forgets the edit, the run and the error alike", async () => {
    updateLeg.mockRejectedValueOnce(new Error("nope"));
    const { editing } = setup([legOver(1, 1, 2)]);
    editing.begin(legOver(1, 1, 2));
    editing.name.value = "Renamed";
    await editing.save();

    editing.cancel();

    expect(editing.editingLegId.value).toBeNull();
    expect(editing.runIds.value.size).toBe(0);
    expect(editing.error.value).toBeNull();
    expect(editing.override.value).toBeNull();
  });
});
