<script setup lang="ts">
defineProps<{
  eyebrow: string;
  title: string;
}>();
</script>

<template>
  <header class="border-b border-base-300 px-5 pb-4 pt-5 md:px-7 md:pb-5 md:pt-6">
    <div class="flex items-center gap-2 font-mono text-[10px] uppercase tracking-[0.28em] text-accent">
      <span class="inline-block h-px w-5 bg-accent/70" />
      {{ eyebrow }}
    </div>
    <h3 class="mt-1.5 font-display text-2xl font-normal tracking-tight text-base-content md:text-3xl">
      {{ title }}
    </h3>
  </header>
</template>
