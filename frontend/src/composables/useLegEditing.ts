import { computed, ref, type ComputedRef, type Ref } from "vue";
import { useQueryCache } from "@pinia/colada";
import type { Leg } from "@/api/web";
import { createLeg, deleteLeg, updateLeg } from "@/api/web";
import {
  claimedSegmentIds,
  segmentsInLegRange,
  toggleContiguous,
  type LegEditOverride,
  type TreeSegment,
} from "@/engine";
import { journeyQueryKey } from "@/composables/useQueries";

/**
 * Editing one leg's name, notes and membership.
 *
 * Membership is the interesting part: a leg is a time range, so its segments are
 * a contiguous run, and clicking a segment while a leg is open adds it to that
 * leg or takes it back out. A segment another leg already owns is refused —
 * it has to be removed from that leg first, because the alternative is a click
 * that silently steals from a leg you cannot see.
 *
 * The pending run is exposed as a `LegEditOverride` so the tree can be rebuilt
 * showing the change before Save. That is also why this lives beside the tree
 * build rather than inside `JourneyTree`: the override is derived from the
 * segments, so a component that received the finished tree could not produce it
 * without a cycle.
 */
export interface UseLegEditingReturn {
  editingLegId: Ref<number | null>;
  name: Ref<string>;
  notes: Ref<string>;
  isSaving: Ref<boolean>;
  /**
   * Why the last write did not land, or null. Held rather than thrown: these are
   * `@click` handlers, so an escaping rejection is a silent failure that leaves
   * the bar looking like nothing happened.
   */
  error: Ref<string | null>;
  /** Pending membership of the leg being edited. */
  runIds: ComputedRef<ReadonlySet<number>>;
  /** Feeds `groupSegments` so the tree previews the pending bounds. */
  override: ComputedRef<LegEditOverride | null>;
  /** Segments owned by a leg other than the one open — unclickable, and dimmed. */
  claimedIds: ComputedRef<ReadonlySet<number>>;
  begin(leg: Leg): void;
  toggleMembership(segmentId: number): void;
  cancel(): void;
  /** Each returns whether the write landed — the caller closes on true only. */
  save(): Promise<boolean>;
  remove(): Promise<boolean>;
  /** Create a leg over an arbitrary run — the screen's own segment selection. */
  create(legName: string, ids: readonly number[], legNotes?: string): Promise<boolean>;
}

export function useLegEditing(
  segments: () => TreeSegment[],
  legs: () => Leg[],
  journeyId: () => number,
): UseLegEditingReturn {
  const queryCache = useQueryCache();

  const editingLegId = ref<number | null>(null);
  const name = ref("");
  const notes = ref("");
  const isSaving = ref(false);
  const error = ref<string | null>(null);
  const pending = ref<Set<number>>(new Set());

  const runIds = computed<ReadonlySet<number>>(() => pending.value);

  const claimedIds = computed<ReadonlySet<number>>(() =>
    claimedSegmentIds(segments(), legs(), editingLegId.value),
  );

  /** A leg spans the first selected segment's start to the last one's end. */
  function boundsOf(ids: ReadonlySet<number> | readonly number[]) {
    const wanted = ids instanceof Set ? ids : new Set(ids);
    const chosen = segments().filter((s) => wanted.has(s.id));
    if (!chosen.length) return null;
    const last = chosen[chosen.length - 1];
    return { start: chosen[0].startedAt, end: last.endedAt ?? last.startedAt };
  }

  const override = computed<LegEditOverride | null>(() =>
    editingLegId.value === null
      ? null
      : {
          legId: editingLegId.value,
          segmentIds: pending.value,
          bounds: boundsOf(pending.value),
        },
  );

  function begin(leg: Leg) {
    editingLegId.value = leg.id;
    name.value = leg.name;
    notes.value = leg.notes ?? "";
    error.value = null;
    pending.value = new Set(segmentsInLegRange(segments(), leg).map((s) => s.id));
  }

  function toggleMembership(segmentId: number) {
    if (editingLegId.value === null) return;
    pending.value = toggleContiguous({
      orderedIds: segments().map((s) => s.id),
      selected: pending.value,
      clickedId: segmentId,
      claimed: claimedIds.value,
    });
  }

  function cancel() {
    editingLegId.value = null;
    name.value = "";
    notes.value = "";
    error.value = null;
    pending.value = new Set();
  }

  /**
   * Runs one write with the spinner on, and reports whether it landed. A failed
   * write leaves the edit exactly as it was — the bar stays open over the same
   * pending run, carrying the reason — because throwing away someone's typing on
   * a dropped connection is worse than the connection dropping.
   */
  async function withSaving(action: string, fn: () => Promise<void>): Promise<boolean> {
    if (isSaving.value) return false;
    isSaving.value = true;
    error.value = null;
    try {
      await fn();
      await queryCache.invalidateQueries({ key: journeyQueryKey(journeyId()) });
      await queryCache.invalidateQueries({ key: ["journeys"] });
      return true;
    } catch (err) {
      error.value = err instanceof Error ? err.message : `Could not ${action} the leg.`;
      return false;
    } finally {
      isSaving.value = false;
    }
  }

  async function save(): Promise<boolean> {
    const legId = editingLegId.value;
    const bounds = boundsOf(pending.value);
    const trimmed = name.value.trim();
    if (legId === null || !bounds || !trimmed) return false;
    const saved = await withSaving("save", async () => {
      await updateLeg(legId, {
        name: trimmed,
        notes: notes.value,
        started_at: bounds.start,
        ended_at: bounds.end,
      });
    });
    if (saved) cancel();
    return saved;
  }

  async function remove(): Promise<boolean> {
    const legId = editingLegId.value;
    if (legId === null) return false;
    const removed = await withSaving("delete", async () => {
      await deleteLeg(legId);
    });
    if (removed) cancel();
    return removed;
  }

  async function create(
    legName: string,
    ids: readonly number[],
    legNotes = "",
  ): Promise<boolean> {
    const bounds = boundsOf(ids);
    const trimmed = legName.trim();
    if (!bounds || !trimmed) return false;
    return withSaving("create", async () => {
      await createLeg({
        journey: journeyId(),
        name: trimmed,
        started_at: bounds.start,
        ended_at: bounds.end,
        notes: legNotes.trim(),
      });
    });
  }

  return {
    editingLegId,
    name,
    notes,
    isSaving,
    error,
    runIds,
    override,
    claimedIds,
    begin,
    toggleMembership,
    cancel,
    save,
    remove,
    create,
  };
}
