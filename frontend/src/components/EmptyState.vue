<script setup lang="ts">
defineProps<{
  eyebrow: string;
  title: string;
  subtitle: string;
}>();
</script>

<template>
  <div
    role="status"
    class="flex min-h-[55vh] flex-col items-center justify-center gap-1.5 px-4 py-12 text-center"
  >
    <span class="mb-1 font-mono text-[11px] font-semibold uppercase tracking-[0.22em] text-accent">
      {{ eyebrow }}
    </span>
    <h2 class="m-0 max-w-[22ch] font-display text-4xl font-normal leading-tight tracking-tight text-base-content">
      {{ title }}
    </h2>
    <p class="mx-0 mb-5 mt-1.5 max-w-[46ch] text-lg text-base-content/50">
      {{ subtitle }}
    </p>
    <slot />
  </div>
</template>
