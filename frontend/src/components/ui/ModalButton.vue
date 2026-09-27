<script setup lang="ts">
withDefaults(
  defineProps<{
    variant?: "ghost" | "primary" | "danger";
    disabled?: boolean;
    type?: "button" | "submit";
  }>(),
  { variant: "ghost", disabled: false, type: "button" },
);
</script>

<template>
  <button
    :type="type"
    :disabled="disabled"
    :class="[
      'font-mono text-[11px] uppercase tracking-[0.28em] transition-colors disabled:cursor-not-allowed',
      variant === 'ghost' && 'px-3 py-2 text-base-content/60 hover:text-base-content disabled:text-base-content/30',
      variant === 'primary' && 'rounded-md bg-accent px-4 py-2 text-accent-content hover:bg-accent/90 disabled:bg-base-300 disabled:text-base-content/30',
      variant === 'danger' && 'rounded-md border border-error/40 bg-error/5 px-4 py-2 text-error hover:bg-error/15',
    ]"
  >
    <slot />
  </button>
</template>
