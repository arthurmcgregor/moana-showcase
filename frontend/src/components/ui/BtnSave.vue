<script setup lang="ts">
withDefaults(
  defineProps<{
    loading?: boolean;
    disabled?: boolean;
    type?: "button" | "submit";
    title?: string;
  }>(),
  { loading: false, disabled: false, type: "submit", title: undefined },
);
</script>

<template>
  <button
    :type="type"
    :disabled="disabled || loading"
    :title="title"
    class="btn-save"
  >
    <span
      v-if="loading"
      class="loading loading-spinner loading-xs"
    />
    <span class="btn-save__label"><slot /></span>
    <OutlineArrowRightIcon
      v-if="!loading"
      class="size-4 btn-save__arrow"
    />
  </button>
</template>

<style scoped>
.btn-save {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 9px 16px 9px 18px;
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0.01em;
  color: var(--color-primary-content);
  background: var(--color-primary);
  border: 0;
  border-radius: 8px;
  cursor: pointer;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.2),
              inset 0 1px 0 color-mix(in oklch, white 20%, transparent);
  transition: transform 120ms ease, box-shadow 120ms ease, background-color 120ms ease, opacity 120ms ease;
}

.btn-save:hover:not(:disabled) {
  background: color-mix(in oklch, var(--color-primary) 90%, white);
  box-shadow: 0 2px 8px color-mix(in oklch, var(--color-primary) 40%, transparent),
              inset 0 1px 0 color-mix(in oklch, white 25%, transparent);
}

.btn-save:hover:not(:disabled) .btn-save__arrow {
  transform: translateX(2px);
}

.btn-save:active:not(:disabled) {
  transform: translateY(1px);
}

.btn-save:disabled {
  opacity: 0.4;
  cursor: not-allowed;
  box-shadow: none;
}

.btn-save__arrow {
  transition: transform 160ms ease;
}
</style>
