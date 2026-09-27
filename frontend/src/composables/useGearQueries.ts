import { computed, ref } from "vue";
import { useQuery, useMutation, useQueryCache } from "@pinia/colada";
import {
  fetchTags,
  fetchItems,
  fetchChecklistState,
  updateChecklistState,
  createItem,
  updateItem,
  deleteItem,
  createTag,
  resetAllDone,
  type Tag,
  type Item,
} from "@/api/gear";

export const gearTagsKey = () => ["gearTags"];
export const gearItemsKey = () => ["gearItems"];
export const gearChecklistStateKey = () => ["gearChecklistState"];

const FIVE_MIN = 1000 * 60 * 5;

export function useGearTagsQuery() {
  return useQuery({
    key: gearTagsKey,
    query: () => fetchTags(),
    staleTime: FIVE_MIN,
  });
}

export function useGearItemsQuery() {
  return useQuery({
    key: gearItemsKey,
    query: () => fetchItems(),
    staleTime: FIVE_MIN,
  });
}

export function useGearChecklistStateQuery() {
  return useQuery({
    key: gearChecklistStateKey,
    query: () => fetchChecklistState(),
    staleTime: FIVE_MIN,
  });
}

/**
 * Derive { categoryName: { tagId, name }[] } and a tagId → categoryName map
 * from a flat list of tags. Top-level tags (parent === null) are categories;
 * their direct children are values. Deeper nesting is ignored by the UI.
 */
export function useTagTree(tagsRef: () => Tag[] | undefined) {
  const categories = computed(() => {
    const tags = tagsRef() ?? [];
    const byId = new Map(tags.map((t) => [t.id, t]));
    const cats = new Map<string, { id: number; values: Tag[] }>();
    for (const t of tags) {
      if (t.parent === null) {
        if (!cats.has(t.name)) cats.set(t.name, { id: t.id, values: [] });
      }
    }
    for (const t of tags) {
      if (t.parent !== null) {
        const parent = byId.get(t.parent);
        if (parent && parent.parent === null) {
          const cat = cats.get(parent.name);
          if (cat) cat.values.push(t);
        }
      }
    }
    return cats;
  });

  const categoryByTagId = computed(() => {
    const tags = tagsRef() ?? [];
    const byId = new Map(tags.map((t) => [t.id, t]));
    const map = new Map<number, string>();
    for (const t of tags) {
      if (t.parent !== null) {
        const parent = byId.get(t.parent);
        if (parent && parent.parent === null) map.set(t.id, parent.name);
      } else {
        map.set(t.id, t.name);
      }
    }
    return map;
  });

  return { categories, categoryByTagId };
}

export function useGearMutations() {
  const queryCache = useQueryCache();
  const mutationError = ref<string | null>(null);

  function invalidateItems() {
    queryCache.invalidateQueries({ key: gearItemsKey() });
  }
  function invalidateTags() {
    queryCache.invalidateQueries({ key: gearTagsKey() });
  }
  function invalidateChecklistState() {
    queryCache.invalidateQueries({ key: gearChecklistStateKey() });
  }

  function recordError(err: unknown) {
    mutationError.value = err instanceof Error ? err.message : String(err);
  }
  function clearError() {
    mutationError.value = null;
  }

  const { mutate: mutateToggleDone } = useMutation({
    mutation: ({ id, done }: { id: number; done: boolean }) => updateItem(id, { done }),
    onMutate: clearError,
    onError: recordError,
    onSettled: invalidateItems,
  });

  const { mutate: mutateSaveItem } = useMutation({
    mutation: (payload: { id?: number; name: string; tags: number[] }) =>
      payload.id
        ? updateItem(payload.id, { name: payload.name, tags: payload.tags })
        : createItem({ name: payload.name, tags: payload.tags }),
    onMutate: clearError,
    onError: recordError,
    onSettled: invalidateItems,
  });

  const { mutate: mutateDeleteItem } = useMutation({
    mutation: (id: number) => deleteItem(id),
    onMutate: clearError,
    onError: recordError,
    onSettled: invalidateItems,
  });

  const { mutate: mutateResetDone } = useMutation({
    mutation: () => resetAllDone(),
    onMutate: clearError,
    onError: recordError,
    onSettled: invalidateItems,
  });

  const { mutate: mutateSelectedTags } = useMutation({
    mutation: (selected_tags: number[]) => updateChecklistState({ selected_tags }),
    onMutate: clearError,
    onError: recordError,
    onSettled: invalidateChecklistState,
  });

  const { mutate: mutateCreateTag } = useMutation({
    mutation: (data: { name: string; parent: number | null }) => createTag(data),
    onMutate: clearError,
    onError: recordError,
    onSettled: invalidateTags,
  });

  return {
    toggleDone: mutateToggleDone,
    saveItem: mutateSaveItem,
    deleteItem: mutateDeleteItem,
    resetDone: mutateResetDone,
    setSelectedTags: mutateSelectedTags,
    createTag: mutateCreateTag,
    mutationError,
    clearError,
  };
}

// ---------------------------------------------------------------------------
// Pure derivation helpers (testable in isolation)
// ---------------------------------------------------------------------------

export const FILTER_CATEGORIES = ["trip_length", "crew", "propulsion"] as const;
export const MODAL_CATEGORY_ORDER = [
  "reminder",
  "trip_length",
  "crew",
  "propulsion",
  "stowage",
] as const;
export const REMINDER_ORDER = ["prep", "pack", "rig", "derig", "post"] as const;
export const CATEGORY_REMINDER = "reminder";
export const CATEGORY_STOWAGE = "stowage";

export interface DerivedItem extends Item {
  reminderPhases: string[];
  stowageLocations: string[];
}

export function decorateItems(
  items: Item[],
  tags: Tag[],
  categoryByTagId: Map<number, string>,
): DerivedItem[] {
  const tagsById = new Map(tags.map((t) => [t.id, t]));
  return items.map((item) => {
    const reminderPhases: string[] = [];
    const stowageLocations: string[] = [];
    for (const tid of item.tags) {
      const cat = categoryByTagId.get(tid);
      const tag = tagsById.get(tid);
      if (!tag) continue;
      if (cat === CATEGORY_REMINDER) reminderPhases.push(tag.name);
      else if (cat === CATEGORY_STOWAGE) stowageLocations.push(tag.name);
    }
    return {
      ...item,
      reminderPhases,
      stowageLocations,
    };
  });
}

/**
 * AND-across-categories, OR-within. Permissive on missing categories: if an
 * item has no tag in a filtered category, it is included for that category.
 */
export function filterItems(
  items: DerivedItem[],
  selectedTagIds: Set<number>,
  categoryByTagId: Map<number, string>,
): DerivedItem[] {
  if (selectedTagIds.size === 0) return items;

  // Group selected tags by category.
  const selectedByCategory = new Map<string, Set<number>>();
  for (const tid of selectedTagIds) {
    const cat = categoryByTagId.get(tid);
    if (!cat) continue;
    if (!selectedByCategory.has(cat)) selectedByCategory.set(cat, new Set());
    selectedByCategory.get(cat)!.add(tid);
  }

  return items.filter((item) => {
    // For each selected category, check: item either has no tag in this
    // category (permissive) OR has at least one matching tag.
    for (const [cat, selectedIds] of selectedByCategory) {
      const itemTagsInCat = item.tags.filter((tid) => categoryByTagId.get(tid) === cat);
      if (itemTagsInCat.length === 0) continue; // permissive
      const hasMatch = itemTagsInCat.some((tid) => selectedIds.has(tid));
      if (!hasMatch) return false;
    }
    return true;
  });
}

/**
 * Group items into reminder phases. Items with no reminder tag and no stowage
 * tag fall into the default phase ("pack"). Items with a stowage tag but no
 * reminder are excluded from the checklist (they live in the stowage view).
 *
 * `phases` should include every reminder value the UI plans to render, so any
 * custom reminder tag values created via the API still get a section instead
 * of silently disappearing.
 */
export function groupByReminderPhase(
  items: DerivedItem[],
  phases: readonly string[],
  defaultPhase: string = "pack",
): Record<string, DerivedItem[]> {
  const groups: Record<string, DerivedItem[]> = {};
  for (const phase of phases) groups[phase] = [];
  if (!groups[defaultPhase]) groups[defaultPhase] = [];

  for (const item of items) {
    if (item.reminderPhases.length > 0) {
      let placed = false;
      for (const phase of item.reminderPhases) {
        if (groups[phase]) {
          groups[phase].push(item);
          placed = true;
        }
      }
      // Defensive: if every reminder phase on the item is unknown to the UI,
      // still surface the item in the default phase so it's not invisible.
      if (!placed && item.stowageLocations.length === 0) {
        groups[defaultPhase].push(item);
      }
    } else if (item.stowageLocations.length === 0) {
      groups[defaultPhase].push(item);
    }
    // else: stowage-only items skipped from checklist
  }
  return groups;
}

/**
 * Build the ordered list of reminder phases to render. Hardcoded
 * `REMINDER_ORDER` names come first (in their canonical order), then any
 * additional reminder tag values the user has created via the API.
 */
export function resolveReminderPhases(reminderValues: readonly string[]): string[] {
  const ordered: string[] = [];
  const seen = new Set<string>();
  for (const name of REMINDER_ORDER) {
    if (reminderValues.includes(name)) {
      ordered.push(name);
      seen.add(name);
    }
  }
  for (const name of reminderValues) {
    if (!seen.has(name)) {
      ordered.push(name);
      seen.add(name);
    }
  }
  return ordered;
}

export function groupByStowageLocation(items: DerivedItem[]): Record<string, DerivedItem[]> {
  const groups: Record<string, DerivedItem[]> = {};
  for (const item of items) {
    for (const loc of item.stowageLocations) {
      if (!groups[loc]) groups[loc] = [];
      groups[loc].push(item);
    }
  }
  return groups;
}
