/**
 * Segment selection for leg creation: always one unbroken run, because a leg is
 * a time range and a gap in the middle of one has no meaning.
 *
 * Clicking outside the run extends to the click; clicking inside it retreats
 * from the nearer end. A run may not swallow a segment another leg already
 * claims, so extending across one is refused outright rather than silently
 * skipping it.
 */

export interface ContiguousSelectionInput {
  /** Every selectable segment id, in time order. */
  orderedIds: number[];
  selected: ReadonlySet<number>;
  clickedId: number;
  /** Segments belonging to some other leg. */
  claimed: ReadonlySet<number>;
}

/**
 * Why a segment will not take a click, or null if it will. This mirrors
 * `toggleContiguous`'s refusals exactly so the UI can dim what it already
 * enforces — a click that silently does nothing is the bug this exists to
 * prevent, and there are two separate reasons for one:
 *
 * - the segment belongs to another leg, or
 * - reaching it means extending across a segment that does, which the run rule
 *   refuses outright rather than swallowing.
 *
 * The second reason is why dimming only the claimed rows is not enough: every
 * segment on the far side of another leg is equally unreachable.
 */
export function blockedReason({
  orderedIds,
  selected,
  clickedId,
  claimed,
}: ContiguousSelectionInput): "claimed" | "unreachable" | null {
  if (claimed.has(clickedId)) return "claimed";

  const clickIdx = orderedIds.indexOf(clickedId);
  // Not a selectable segment at all — `toggleContiguous` refuses it too, and a
  // refusal the two disagree about is exactly what this function exists to stop.
  if (clickIdx === -1) return "unreachable";
  // With nothing selected any unclaimed segment starts a fresh run.
  if (selected.size === 0) return null;

  const indices = orderedIds
    .map((id, i) => (selected.has(id) ? i : -1))
    .filter((i) => i !== -1);
  const min = Math.min(...indices);
  const max = Math.max(...indices);

  // Inside the run, a click always retreats — never blocked.
  if (clickIdx >= min && clickIdx <= max) return null;

  const lo = Math.min(min, clickIdx);
  const hi = Math.max(max, clickIdx);
  for (let i = lo; i <= hi; i++) {
    if (claimed.has(orderedIds[i])) return "unreachable";
  }
  return null;
}

export function toggleContiguous({
  orderedIds,
  selected,
  clickedId,
  claimed,
}: ContiguousSelectionInput): Set<number> {
  if (claimed.has(clickedId)) return new Set(selected);

  const clickIdx = orderedIds.indexOf(clickedId);
  if (clickIdx === -1) return new Set(selected);

  if (selected.size === 0) return new Set([clickedId]);

  const indices = orderedIds
    .map((id, i) => (selected.has(id) ? i : -1))
    .filter((i) => i !== -1);
  const min = Math.min(...indices);
  const max = Math.max(...indices);

  if (clickIdx < min || clickIdx > max) {
    const lo = Math.min(min, clickIdx);
    const hi = Math.max(max, clickIdx);
    for (let i = lo; i <= hi; i++) {
      if (claimed.has(orderedIds[i])) return new Set(selected);
    }
    const next = new Set<number>();
    for (let i = lo; i <= hi; i++) next.add(orderedIds[i]);
    return next;
  }

  // Inside the run: drop everything between the click and the nearer end, which
  // is the only way to shrink it without splitting it in two.
  const next = new Set(selected);
  if (clickIdx - min <= max - clickIdx) {
    for (let i = min; i <= clickIdx; i++) next.delete(orderedIds[i]);
  } else {
    for (let i = clickIdx; i <= max; i++) next.delete(orderedIds[i]);
  }
  return next;
}
