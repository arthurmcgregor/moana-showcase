<script setup lang="ts">
import { computed } from "vue";
import { STAT_ICON_PATHS, type StatIconName } from "./statIcons";

const props = defineProps<{ name: StatIconName }>();

const path = computed(() => STAT_ICON_PATHS[props.name]);
</script>

<template>
  <svg
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="1.5"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path :d="path" />
  </svg>
</template>
