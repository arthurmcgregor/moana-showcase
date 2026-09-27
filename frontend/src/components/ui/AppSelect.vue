<script setup lang="ts" generic="T extends string | number | null">
const model = defineModel<T>();

defineProps<{
  disabled?: boolean;
}>();
</script>

<template>
  <div class="select-wrap">
    <select
      v-model="model"
      class="ui-input ui-input--select"
      :disabled="disabled"
    >
      <slot />
    </select>
    <OutlineChevronDownIcon class="select-wrap__chevron size-4" />
  </div>
</template>

<style scoped>
.select-wrap {
  position: relative;
}

.ui-input--select {
  appearance: none;
  padding-right: 36px;
  cursor: pointer;
}

.select-wrap__chevron {
  position: absolute;
  top: 50%;
  right: 12px;
  pointer-events: none;
  color: color-mix(in oklch, var(--color-base-content) 45%, transparent);
  transform: translateY(-50%);
}
</style>
