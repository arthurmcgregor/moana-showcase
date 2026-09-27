<template>
  <div class="action-group">
    <slot />
  </div>
</template>

<style scoped>
.action-group {
  display: inline-flex;
  align-items: center;
  padding: 2px;
  background: color-mix(in oklch, var(--color-base-content) 3%, transparent);
  border: 1px solid color-mix(in oklch, var(--color-base-content) 10%, transparent);
  border-radius: 8px;
  gap: 2px;
}
</style>
