<script setup lang="ts">
withDefaults(
  defineProps<{
    variant?: "default" | "danger";
    active?: boolean;
    title?: string;
    type?: "button" | "submit" | "reset";
  }>(),
  { variant: "default", active: false, type: "button", title: undefined },
);
</script>

<template>
  <button
    :type="type"
    :title="title"
    class="action-btn"
    :class="{
      'action-btn--active': active,
      'action-btn--danger': variant === 'danger',
    }"
  >
    <slot />
    <span
      v-if="active"
      class="action-btn__indicator"
      aria-hidden="true"
    />
  </button>
</template>

<style scoped>
.action-btn {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 28px;
  padding: 0;
  background: transparent;
  border: 0;
  border-radius: 6px;
  color: color-mix(in oklch, var(--color-base-content) 70%, transparent);
  cursor: pointer;
  transition: background-color 120ms ease, color 120ms ease;
}

.action-btn:hover {
  background: color-mix(in oklch, var(--color-base-content) 8%, transparent);
  color: var(--color-base-content);
}

.action-btn--active {
  background: color-mix(in oklch, var(--color-primary) 18%, transparent);
  color: var(--color-primary);
}

.action-btn--active:hover {
  background: color-mix(in oklch, var(--color-primary) 28%, transparent);
  color: var(--color-primary);
}

.action-btn--danger:hover {
  background: color-mix(in oklch, var(--color-error) 14%, transparent);
  color: var(--color-error);
}

.action-btn__indicator {
  position: absolute;
  top: 4px;
  right: 4px;
  width: 6px;
  height: 6px;
  border-radius: 999px;
  background: var(--color-primary);
  box-shadow: 0 0 0 2px var(--color-base-100);
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.45; }
}
</style>
