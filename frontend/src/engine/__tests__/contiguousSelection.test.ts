import { describe, it, expect } from "vitest";
import { blockedReason, toggleContiguous } from "../contiguousSelection";

const IDS = [1, 2, 3, 4, 5, 6];

function toggle(
  selected: number[],
  clickedId: number,
  claimed: number[] = [],
  orderedIds = IDS,
): number[] {
  return [
    ...toggleContiguous({
      orderedIds,
      selected: new Set(selected),
      clickedId,
      claimed: new Set(claimed),
    }),
  ].sort((a, b) => a - b);
}

describe("toggleContiguous", () => {
  it("starts a run from nothing", () => {
    expect(toggle([], 3)).toEqual([3]);
  });

  it("extends forwards, filling the gap", () => {
    expect(toggle([2], 5)).toEqual([2, 3, 4, 5]);
  });

  it("extends backwards, filling the gap", () => {
    expect(toggle([5], 2)).toEqual([2, 3, 4, 5]);
  });

  it("retreats from the nearer end when clicked inside the run", () => {
    expect(toggle([2, 3, 4, 5, 6], 3)).toEqual([4, 5, 6]);
    expect(toggle([1, 2, 3, 4, 5], 4)).toEqual([1, 2, 3]);
  });

  it("empties the run when its only member is clicked", () => {
    expect(toggle([3], 3)).toEqual([]);
  });

  it("refuses to extend across a segment another leg owns", () => {
    // 4 belongs elsewhere, so 2→6 would have to swallow it.
    expect(toggle([2], 6, [4])).toEqual([2]);
  });

  it("extends up to, but not past, a claimed segment", () => {
    expect(toggle([2], 3, [4])).toEqual([2, 3]);
  });

  it("ignores a click on a claimed segment", () => {
    expect(toggle([2, 3], 4, [4])).toEqual([2, 3]);
  });

  it("ignores a click on an id it does not know", () => {
    expect(toggle([2, 3], 99)).toEqual([2, 3]);
  });

  it("never leaves a gap in the middle", () => {
    const runs = [
      toggle([1], 4),
      toggle([1, 2, 3, 4], 2),
      toggle([3, 4, 5], 1),
    ];
    for (const run of runs) {
      if (run.length < 2) continue;
      const positions = run.map((id) => IDS.indexOf(id));
      expect(positions[positions.length - 1] - positions[0]).toBe(run.length - 1);
    }
  });

  it("leaves the selection untouched when the click changes nothing", () => {
    const selected = new Set([2, 3]);
    const next = toggleContiguous({
      orderedIds: IDS,
      selected,
      clickedId: 99,
      claimed: new Set(),
    });
    expect(next).not.toBe(selected);
    expect([...next]).toEqual([2, 3]);
  });
});

describe("blockedReason", () => {
  function reason(selected: number[], clickedId: number, claimed: number[] = []) {
    return blockedReason({
      orderedIds: IDS,
      selected: new Set(selected),
      clickedId,
      claimed: new Set(claimed),
    });
  }

  it("names a segment another leg owns", () => {
    expect(reason([2], 4, [4])).toBe("claimed");
  });

  it("names the segments stranded on the far side of one", () => {
    // 4 is owned, so 5 and 6 cannot be reached from a run ending at 2 — the run
    // rule refuses to extend across 4 rather than skipping it.
    expect(reason([2], 5, [4])).toBe("unreachable");
    expect(reason([2], 6, [4])).toBe("unreachable");
  });

  it("blocks nothing reachable on the near side", () => {
    expect(reason([2], 3, [4])).toBeNull();
    expect(reason([2], 1, [4])).toBeNull();
  });

  it("blocks nothing but the owned segments when the run is empty", () => {
    expect(reason([], 6, [4])).toBeNull();
    expect(reason([], 4, [4])).toBe("claimed");
  });

  it("never blocks a click inside the run, which only ever retreats", () => {
    expect(reason([2, 3, 4, 5], 3)).toBeNull();
  });

  it("calls an id that is not a segment at all unreachable, as toggleContiguous does", () => {
    expect(reason([2], 99)).toBe("unreachable");
    expect(reason([], 99)).toBe("unreachable");
  });

  it("agrees with toggleContiguous on every id, which is the point of it", () => {
    const selected = [2, 3];
    const claimed = [5];
    for (const id of [...IDS, 99]) {
      const before = [...selected].sort((a, b) => a - b);
      const after = toggle(selected, id, claimed);
      const refused = after.length === before.length && after.every((v, i) => v === before[i]);
      // A click inside the run legitimately returns the same set only when it is
      // a no-op retreat, which cannot happen for this run.
      expect(refused).toBe(reason(selected, id, claimed) !== null);
    }
  });
});
