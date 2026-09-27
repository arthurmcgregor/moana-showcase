<script setup lang="ts">
withDefaults(
  defineProps<{
    label: string;
    tone?: "default" | "danger";
  }>(),
  { tone: "default" },
);
</script>

<template>
  <div class="mb-2 flex items-baseline gap-2">
    <span
      class="font-mono text-[10px] uppercase tracking-[0.2em]"
      :class="tone === 'danger' ? 'text-error/80' : 'text-base-content/40'"
    >
      {{ label }}
    </span>
    <span
      class="h-px grow"
      :class="tone === 'danger' ? 'bg-error/25' : 'bg-base-300/60'"
    />
    <span
      v-if="$slots.trailing"
      class="font-mono text-[10px] uppercase tracking-[0.2em]"
      :class="tone === 'danger' ? 'text-error/60' : 'text-base-content/40'"
    >
      <slot name="trailing" />
    </span>
  </div>
</template>
