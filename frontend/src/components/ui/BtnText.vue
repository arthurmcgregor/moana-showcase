<script setup lang="ts">
withDefaults(
  defineProps<{
    disabled?: boolean;
    type?: "button" | "submit" | "reset";
  }>(),
  { disabled: false, type: "button" },
);
</script>

<template>
  <button
    :type="type"
    :disabled="disabled"
    class="btn-text"
  >
    <slot />
  </button>
</template>

<style scoped>
.btn-text {
  padding: 9px 14px;
  font-size: 13px;
  font-weight: 500;
  color: color-mix(in oklch, var(--color-base-content) 60%, transparent);
  background: transparent;
  border: 0;
  border-radius: 8px;
  cursor: pointer;
  transition: color 120ms ease, background-color 120ms ease;
}

.btn-text:hover:not(:disabled) {
  color: var(--color-base-content);
  background: color-mix(in oklch, var(--color-base-content) 6%, transparent);
}

.btn-text:disabled {
  opacity: 0.35;
  cursor: not-allowed;
}
</style>
