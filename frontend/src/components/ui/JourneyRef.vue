<script setup lang="ts">
import type { RouteLocationRaw } from "vue-router";

defineProps<{
  to: RouteLocationRaw;
  name: string;
}>();
</script>

<template>
  <router-link
    :to="to"
    class="journey-ref"
  >
    <span
      class="journey-ref__dot"
      aria-hidden="true"
    />
    <span class="journey-ref__name">{{ name }}</span>
    <OutlineArrowRightIcon class="journey-ref__arrow size-3.5" />
  </router-link>
</template>

<style scoped>
.journey-ref {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 4px 10px;
  font-size: 12px;
  font-weight: 500;
  color: color-mix(in oklch, var(--color-base-content) 80%, transparent);
  background: color-mix(in oklch, var(--color-base-content) 4%, transparent);
  border: 1px solid color-mix(in oklch, var(--color-base-content) 10%, transparent);
  border-radius: 999px;
  text-decoration: none;
  transition: background-color 140ms ease, border-color 140ms ease, color 140ms ease, transform 140ms ease;
}

.journey-ref:hover {
  color: var(--color-primary);
  background: color-mix(in oklch, var(--color-primary) 10%, transparent);
  border-color: color-mix(in oklch, var(--color-primary) 35%, transparent);
}

.journey-ref:hover .journey-ref__arrow {
  transform: translateX(2px);
  opacity: 1;
}

.journey-ref__dot {
  width: 6px;
  height: 6px;
  border-radius: 999px;
  background: var(--color-primary);
  box-shadow: 0 0 0 3px color-mix(in oklch, var(--color-primary) 18%, transparent);
  flex-shrink: 0;
}

.journey-ref__name {
  max-width: 220px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.journey-ref__arrow {
  opacity: 0.5;
  transition: transform 160ms ease, opacity 160ms ease;
}
</style>
