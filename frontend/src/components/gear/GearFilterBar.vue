<script setup lang="ts">
import { computed } from "vue";
import type { Tag } from "@/api/gear";
import { FILTER_CATEGORIES } from "@/composables/useGearQueries";

const props = defineProps<{
  categories: Map<string, { id: number; values: Tag[] }>;
  selectedTagIds: Set<number>;
}>();

const emit = defineEmits<{
  toggle: [tagId: number];
}>();

type VisibleCategory = { name: string; cat: { id: number; values: Tag[] } };

const visibleCategories = computed<VisibleCategory[]>(() => {
  const out: VisibleCategory[] = [];
  for (const name of FILTER_CATEGORIES) {
    const cat = props.categories.get(name);
    if (cat && cat.values.length > 0) out.push({ name, cat });
  }
  return out;
});

function isSelected(id: number): boolean {
  return props.selectedTagIds.has(id);
}
</script>

<template>
  <div class="space-y-3 rounded-lg border border-base-300 bg-base-100 p-4">
    <div
      v-for="{ name, cat } in visibleCategories"
      :key="name"
      class="flex flex-wrap items-center gap-2"
    >
      <span class="w-24 font-mono text-[10px] uppercase tracking-[0.18em] text-base-content/50">
        {{ name.replace("_", " ") }}
      </span>
      <button
        v-for="tag in cat.values"
        :key="tag.id"
        type="button"
        class="filter-chip"
        :class="{ 'filter-chip--on': isSelected(tag.id) }"
        @click="emit('toggle', tag.id)"
      >
        {{ tag.name }}
      </button>
    </div>
  </div>
</template>

<style scoped>
.filter-chip {
  display: inline-flex;
  align-items: center;
  padding: 4px 10px;
  border-radius: 999px;
  border: 1px solid var(--color-base-300);
  background: var(--color-base-100);
  color: var(--color-base-content);
  font-size: 12px;
  opacity: 0.7;
  cursor: pointer;
  transition: background-color 120ms ease, border-color 120ms ease, opacity 120ms ease;
}

.filter-chip:hover {
  opacity: 1;
  border-color: color-mix(in oklch, var(--color-base-content) 25%, transparent);
}

.filter-chip--on {
  opacity: 1;
  background: color-mix(in oklch, var(--color-accent) 18%, transparent);
  border-color: var(--color-accent);
  color: var(--color-base-content);
}
</style>
