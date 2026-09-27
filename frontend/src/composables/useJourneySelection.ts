import { computed, type ComputedRef } from "vue";
import { useRoute, useRouter } from "vue-router";
import { JOURNEY_SCOPE, scopesEqual, type Scope } from "@/engine";

export interface UseJourneySelectionReturn {
  scope: ComputedRef<Scope>;
  select(next: Scope): void;
  /** Set the scope without the toggle-off rule. For gestures that aim at a place. */
  selectOnly(next: Scope): void;
  clear(): void;
}

function parseId(value: unknown): number | null {
  if (typeof value !== "string") return null;
  const id = Number(value);
  return Number.isInteger(id) && id > 0 ? id : null;
}

/** `?seg=4` or `?seg=4,5,6` — a run of one is still a run. Junk ids drop out. */
function parseIds(value: unknown): number[] {
  if (typeof value !== "string") return [];
  const seen = new Set<number>();
  for (const part of value.split(",")) {
    const id = parseId(part.trim());
    if (id !== null) seen.add(id);
  }
  return [...seen];
}

/**
 * What the journey screen is scoped to, held in the query string and nowhere
 * else — `?seg=4` or `?leg=2`, one or neither.
 *
 * Only the selection is routed. The playhead and the map's zoom are not, so
 * scrubbing and panning never write history entries; selection itself uses
 * `replace` for the same reason. A leg is derivable from the segment inside it,
 * so the two parameters are never written together and cannot contradict.
 */
export function useJourneySelection(): UseJourneySelectionReturn {
  const route = useRoute();
  const router = useRouter();

  const scope = computed<Scope>(() => {
    const segmentIds = parseIds(route.query.seg);
    if (segmentIds.length) return { kind: "segments", ids: segmentIds };
    const legId = parseId(route.query.leg);
    if (legId !== null) return { kind: "leg", id: legId };
    return JOURNEY_SCOPE;
  });

  function apply(target: Scope) {
    const query = { ...route.query };
    delete query.leg;
    delete query.seg;
    if (target.kind === "leg") query.leg = String(target.id);
    // An empty run is no selection at all, so it writes nothing and reads back
    // as the whole journey.
    if (target.kind === "segments" && target.ids.length) query.seg = target.ids.join(",");
    router.replace({ query });
  }

  // Selecting what is already selected returns to the whole journey.
  function select(next: Scope) {
    apply(scopesEqual(next, scope.value) ? JOURNEY_SCOPE : next);
  }

  /**
   * Clicking a place on the deck's bars means "scope to what is here", so it has
   * no toggle-off: scrubbing back and forth within one segment would otherwise
   * flip the scope on and off under the pointer.
   */
  function selectOnly(next: Scope) {
    if (scopesEqual(next, scope.value)) return;
    apply(next);
  }

  function clear() {
    apply(JOURNEY_SCOPE);
  }

  return { scope, select, selectOnly, clear };
}
