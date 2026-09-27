<script setup lang="ts">
defineProps<{
  title: string;
  subtitle?: string;
}>();
</script>

<template>
  <header class="mb-4 px-4 md:mb-8 md:px-0">
    <h2 class="font-display text-2xl font-normal tracking-tight text-base-content md:text-3xl">
      {{ title }}
    </h2>
    <p
      v-if="subtitle"
      class="mt-1 text-sm text-base-content/50 md:text-base"
    >
      {{ subtitle }}
    </p>
  </header>
</template>
