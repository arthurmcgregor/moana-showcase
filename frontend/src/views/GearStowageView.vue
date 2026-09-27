<script setup lang="ts">
import { computed, ref } from "vue";
import { useRouter } from "vue-router";
import type { Item } from "@/api/gear";
import {
  useGearTagsQuery,
  useGearItemsQuery,
  useGearMutations,
  useTagTree,
  decorateItems,
  groupByStowageLocation,
  CATEGORY_STOWAGE,
} from "@/composables/useGearQueries";

const router = useRouter();

const tagsQuery = useGearTagsQuery();
const itemsQuery = useGearItemsQuery();
const mutations = useGearMutations();
const mutationError = mutations.mutationError;

const { categories, categoryByTagId } = useTagTree(() => tagsQuery.state.value.data);

const decorated = computed(() =>
  decorateItems(
    itemsQuery.state.value.data ?? [],
    tagsQuery.state.value.data ?? [],
    categoryByTagId.value,
  ),
);

const grouped = computed(() => groupByStowageLocation(decorated.value));

const stowageLocationOrder = computed(() => {
  const cat = categories.value.get(CATEGORY_STOWAGE);
  return cat ? cat.values.map((t) => t.name) : [];
});

const visibleLocations = computed(() =>
  stowageLocationOrder.value.filter((loc) => (grouped.value[loc]?.length ?? 0) > 0),
);

const loading = computed(
  () =>
    tagsQuery.state.value.status === "pending" ||
    itemsQuery.state.value.status === "pending",
);

const error = computed(
  () => tagsQuery.state.value.error || itemsQuery.state.value.error,
);

const showModal = ref(false);
const editingItem = ref<Item | null>(null);

function openCreate() {
  editingItem.value = null;
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
          aria-selected="false"
          class="gear-tab"
          @click="router.push('/gear')"
        >
          Checklist
        </button>
        <button
          role="tab"
          aria-selected="true"
          class="gear-tab gear-tab--active"
        >
          Stowage
        </button>
      </nav>
      <BtnSave @click="openCreate">
        + New item
      </BtnSave>
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

      <div
        v-if="visibleLocations.length === 0"
        class="rounded-lg border border-base-300 bg-base-100 px-4 py-8 text-center text-base-content/60"
      >
        No items stowed yet. Tag an item with a stowage location to see it here.
      </div>

      <section
        v-for="loc in visibleLocations"
        :key="loc"
        class="mb-4"
      >
        <h2 class="mb-2 px-4 font-mono text-[11px] uppercase tracking-[0.28em] text-accent md:px-0">
          {{ loc.replace("-", " ") }}
        </h2>
        <ul class="rounded-lg border border-base-300 bg-base-100">
          <GearItemRow
            v-for="item in grouped[loc]"
            :key="`${loc}-${item.id}`"
            :item="item"
            @edit="openEdit(item)"
          />
        </ul>
      </section>
    </template>

    <GearItemModal
      :show="showModal"
      :item="editingItem"
      :categories="categories"
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
</style>
