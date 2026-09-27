<script setup lang="ts">
defineProps<{
  rows: Array<{ label: string; value: string }>;
}>();
</script>

<template>
  <dl class="grid grid-cols-[auto_1fr] gap-x-6 gap-y-1 font-mono text-xs">
    <template
      v-for="row in rows"
      :key="row.label"
    >
      <dt class="uppercase tracking-[0.2em] text-base-content/40">
        {{ row.label }}
      </dt>
      <dd class="tabular-nums text-base-content/80">
        {{ row.value }}
      </dd>
    </template>
  </dl>
</template>
