<script setup lang="ts">
import { computed, ref } from "vue";
import { useRouter } from "vue-router";
import type { Item } from "@/api/gear";
import {
  useGearTagsQuery,
  useGearItemsQuery,
  useGearChecklistStateQuery,
  useGearMutations,
  useTagTree,
  decorateItems,
  filterItems,
  groupByReminderPhase,
  resolveReminderPhases,
  CATEGORY_REMINDER,
} from "@/composables/useGearQueries";

const router = useRouter();

const tagsQuery = useGearTagsQuery();
const itemsQuery = useGearItemsQuery();
const stateQuery = useGearChecklistStateQuery();
const mutations = useGearMutations();
const mutationError = mutations.mutationError;

const { categories, categoryByTagId } = useTagTree(() => tagsQuery.state.value.data);

const selectedTagIds = computed(
  () => new Set(stateQuery.state.value.data?.selected_tags ?? []),
);

const decorated = computed(() =>
  decorateItems(
    itemsQuery.state.value.data ?? [],
    tagsQuery.state.value.data ?? [],
    categoryByTagId.value,
  ),
);

const filtered = computed(() =>
  filterItems(decorated.value, selectedTagIds.value, categoryByTagId.value),
);

const reminderPhases = computed(() => {
  const reminderCat = categories.value.get(CATEGORY_REMINDER);
  const values = reminderCat ? reminderCat.values.map((t) => t.name) : [];
  return resolveReminderPhases(values);
});

const grouped = computed(() => groupByReminderPhase(filtered.value, reminderPhases.value));

const reminderTagIdByName = computed(() => {
  const reminderCat = categories.value.get(CATEGORY_REMINDER);
  const map = new Map<string, number>();
  if (!reminderCat) return map;
  for (const tag of reminderCat.values) map.set(tag.name, tag.id);
  return map;
});

// Items that exist but won't appear in the checklist for any reason — used to
// surface "X items not shown" affordances so users can find them.
const stowageOnlyCount = computed(
  () => filtered.value.filter(
    (i) => i.reminderPhases.length === 0 && i.stowageLocations.length > 0,
  ).length,
);

const filterHiddenCount = computed(
  () => decorated.value.length - filtered.value.length,
);

const loading = computed(
  () =>
    tagsQuery.state.value.status === "pending" ||
    itemsQuery.state.value.status === "pending" ||
    stateQuery.state.value.status === "pending",
);

const error = computed(
  () =>
    tagsQuery.state.value.error ||
    itemsQuery.state.value.error ||
    stateQuery.state.value.error,
);

const showModal = ref(false);
const editingItem = ref<Item | null>(null);
const createDefaultTagIds = ref<number[]>([]);

function openCreate(phase: string = "pack") {
  editingItem.value = null;
  const tagId = reminderTagIdByName.value.get(phase);
  createDefaultTagIds.value = tagId ? [tagId] : [];
  showModal.value = true;
}

function openEdit(item: Item) {
  editingItem.value = item;
  showModal.value = true;
}

function handleSave(payload: { id?: number; name: string; tags: number[] }) {
  mutations.saveItem(payload);
  showModal.value = false;
}

function handleDelete(id: number) {
  mutations.deleteItem(id);
  showModal.value = false;
}

function toggleFilterTag(tagId: number) {
  const next = new Set(selectedTagIds.value);
  if (next.has(tagId)) next.delete(tagId);
  else next.add(tagId);
  mutations.setSelectedTags(Array.from(next));
}

function onResetDone() {
  if (confirm("Clear all done checks?")) {
    mutations.resetDone();
  }
}

function clearFilter() {
  mutations.setSelectedTags([]);
}
</script>

<template>
  <div>
    <PageHeader title="Gear" />

    <div class="mb-4 flex flex-wrap items-center justify-between gap-2 px-4 md:px-0">
      <nav
        role="tablist"
        class="flex gap-1"
      >
        <button
          role="tab"
          aria-selected="true"
          class="gear-tab gear-tab--active"
        >
          Checklist
        </button>
        <button
          role="tab"
          aria-selected="false"
          class="gear-tab"
          @click="router.push('/gear/stowage')"
        >
          Stowage
        </button>
      </nav>
      <div class="flex gap-2">
        <BtnText @click="onResetDone">
          Reset
        </BtnText>
        <BtnSave @click="openCreate">
          + New item
        </BtnSave>
      </div>
    </div>

    <div
      v-if="loading"
      class="px-4 py-8 text-center text-base-content/60"
    >
      Loading gear…
    </div>

    <AppNotice
      v-else-if="error"
      variant="error"
    >
      {{ String(error) }}
    </AppNotice>

    <template v-else>
      <AppNotice
        v-if="mutationError"
        variant="error"
        class="mb-3"
      >
        Couldn't save: {{ mutationError }}
      </AppNotice>

      <div class="mb-4 px-4 md:px-0">
        <GearFilterBar
          :categories="categories"
          :selected-tag-ids="selectedTagIds"
          @toggle="toggleFilterTag"
        />
      </div>

      <section
        v-for="phase in reminderPhases"
        :key="phase"
        class="mb-4"
      >
        <header class="mb-2 flex items-center justify-between px-4 md:px-0">
          <h2 class="font-mono text-[11px] uppercase tracking-[0.28em] text-accent">
            {{ phase }}
          </h2>
          <button
            type="button"
            class="phase-add-btn"
            :title="`Add item to ${phase}`"
            @click="openCreate(phase)"
          >
            +
          </button>
        </header>
        <ul
          v-if="(grouped[phase]?.length ?? 0) > 0"
          class="rounded-lg border border-base-300 bg-base-100"
        >
          <GearItemRow
            v-for="item in grouped[phase]"
            :key="`${phase}-${item.id}`"
            :item="item"
            show-checkbox
            @toggle="mutations.toggleDone({ id: item.id, done: !item.done })"
            @edit="openEdit(item)"
          />
        </ul>
        <p
          v-else
          class="rounded-lg border border-dashed border-base-300/60 px-4 py-3 text-xs text-base-content/40"
        >
          No items.
        </p>
      </section>

      <footer
        v-if="filterHiddenCount > 0 || stowageOnlyCount > 0"
        class="mt-4 space-y-2 px-4 text-xs text-base-content/60 md:px-0"
      >
        <p v-if="filterHiddenCount > 0">
          {{ filterHiddenCount }} {{ filterHiddenCount === 1 ? "item" : "items" }}
          hidden by the active filter.
          <button
            type="button"
            class="font-mono text-[10px] uppercase tracking-[0.18em] text-accent underline-offset-2 hover:underline"
            @click="clearFilter"
          >
            Clear filter
          </button>
        </p>
        <p v-if="stowageOnlyCount > 0">
          {{ stowageOnlyCount }} {{ stowageOnlyCount === 1 ? "item" : "items" }}
          tagged only with a stowage location —
          <button
            type="button"
            class="font-mono text-[10px] uppercase tracking-[0.18em] text-accent underline-offset-2 hover:underline"
            @click="router.push('/gear/stowage')"
          >
            See stowage
          </button>
        </p>
      </footer>
    </template>

    <GearItemModal
      :show="showModal"
      :item="editingItem"
      :categories="categories"
      :default-tag-ids="createDefaultTagIds"
      @cancel="showModal = false"
      @save="handleSave"
      @remove="handleDelete"
    />
  </div>
</template>

<style scoped>
.gear-tab {
  padding: 6px 12px;
  border-radius: 999px;
  font-family: var(--font-mono);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.18em;
  opacity: 0.55;
  cursor: pointer;
  background: transparent;
  border: 1px solid transparent;
  color: var(--color-base-content);
  transition: opacity 120ms ease, background-color 120ms ease, border-color 120ms ease;
}

.gear-tab:hover {
  opacity: 0.9;
  background: color-mix(in oklch, var(--color-base-content) 6%, transparent);
}

.gear-tab--active {
  opacity: 1;
  background: color-mix(in oklch, var(--color-accent) 14%, transparent);
  border-color: color-mix(in oklch, var(--color-accent) 50%, transparent);
  color: var(--color-base-content);
}

.phase-add-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  border-radius: 999px;
  border: 1px solid color-mix(in oklch, var(--color-accent) 40%, transparent);
  background: transparent;
  color: var(--color-accent);
  font-size: 16px;
  line-height: 1;
  cursor: pointer;
  opacity: 0.7;
  transition: background-color 120ms ease, opacity 120ms ease;
}

.phase-add-btn:hover {
  opacity: 1;
  background: color-mix(in oklch, var(--color-accent) 14%, transparent);
}
</style>
