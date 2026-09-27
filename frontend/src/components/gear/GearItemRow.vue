<script setup lang="ts">
import type { DerivedItem } from "@/composables/useGearQueries";

const props = defineProps<{
  item: DerivedItem;
  showCheckbox?: boolean;
}>();

const emit = defineEmits<{
  toggle: [];
  edit: [];
}>();

// Vue's `:checked` patcher skips DOM writes when the bound value is unchanged
// (e.g. mutation failed → item.done stays false), so a user's local click
// stays visually checked even though the data says otherwise. Reset the DOM
// to the data's truth synchronously so the user sees nothing apparent change
// until the refetch confirms the real new state.
function onCheckboxChange(e: Event) {
  (e.target as HTMLInputElement).checked = props.item.done;
  emit("toggle");
}
</script>

<template>
  <li class="flex items-center gap-3 border-b border-base-300/60 px-4 py-2 last:border-b-0">
    <input
      v-if="showCheckbox"
      type="checkbox"
      class="checkbox checkbox-sm checkbox-accent"
      :checked="item.done"
      @change="onCheckboxChange"
    >
    <button
      type="button"
      class="flex flex-1 items-center gap-3 text-left"
      @click="emit('edit')"
    >
      <span
        class="flex-1 truncate"
        :class="{ 'line-through opacity-50': item.done && showCheckbox }"
      >
        {{ item.name }}
      </span>
    </button>
  </li>
</template>
