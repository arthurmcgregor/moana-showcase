<script setup lang="ts" generic="T extends string | number | null">
import type { Component } from "vue";

const model = defineModel<T>();

defineProps<{
  icon?: Component;
  title?: string;
  maxWidth?: number;
}>();
</script>

<template>
  <label
    class="icon-select group"
    :title="title"
  >
    <component
      :is="icon"
      v-if="icon"
      class="icon-select__icon size-4 shrink-0 text-base-content/50 transition-colors group-focus-within:text-primary"
    />
    <select
      v-model="model"
      class="icon-select__native"
      :style="maxWidth ? { maxWidth: `${maxWidth}px` } : undefined"
    >
      <slot />
    </select>
    <OutlineChevronDownIcon class="size-3.5 shrink-0 text-base-content/40" />
  </label>
</template>

<style scoped>
.icon-select {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 0 10px 0 12px;
  height: 32px;
  background: color-mix(in oklch, var(--color-base-content) 3%, transparent);
  border: 1px solid color-mix(in oklch, var(--color-base-content) 10%, transparent);
  border-radius: 8px;
  cursor: pointer;
  transition: background-color 120ms ease, border-color 120ms ease;
}

.icon-select:hover {
  background: color-mix(in oklch, var(--color-base-content) 6%, transparent);
  border-color: color-mix(in oklch, var(--color-base-content) 18%, transparent);
}

.icon-select:focus-within {
  border-color: color-mix(in oklch, var(--color-primary) 60%, transparent);
  box-shadow: 0 0 0 3px color-mix(in oklch, var(--color-primary) 15%, transparent);
}

.icon-select__native {
  appearance: none;
  background: transparent;
  border: 0;
  outline: 0;
  padding: 0;
  font-size: 13px;
  font-weight: 500;
  color: var(--color-base-content);
  cursor: pointer;
  max-width: 160px;
  text-overflow: ellipsis;
}
</style>
