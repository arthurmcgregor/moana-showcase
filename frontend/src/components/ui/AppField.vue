<script setup lang="ts">
defineProps<{
  label: string;
  hint?: string;
}>();
</script>

<template>
  <label class="field">
    <span class="field__label">{{ label }}</span>
    <slot />
    <span
      v-if="hint"
      class="field__hint"
    >{{ hint }}</span>
  </label>
</template>

<style scoped>
.field {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.field__label {
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  color: color-mix(in oklch, var(--color-base-content) 40%, transparent);
}

.field__hint {
  font-size: 12px;
  color: color-mix(in oklch, var(--color-base-content) 45%, transparent);
}
</style>
