<script setup lang="ts">
defineProps<{
  file: File;
}>();

defineEmits<{
  remove: [];
}>();

function formatSize(bytes: number) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}
</script>

<template>
  <div class="file-chip">
    <div class="file-chip__icon">
      <OutlineDocumentTextIcon class="size-5" />
    </div>
    <div class="file-chip__meta">
      <p class="file-chip__name">
        {{ file.name }}
      </p>
      <p class="file-chip__size">
        {{ formatSize(file.size) }}
      </p>
    </div>
    <button
      type="button"
      class="file-chip__remove"
      title="Remove file"
      @click="$emit('remove')"
    >
      <OutlineXMarkIcon class="size-4" />
    </button>
  </div>
</template>

<style scoped>
.file-chip {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 12px 12px 14px;
  background: color-mix(in oklch, var(--color-base-content) 3%, transparent);
  border: 1px solid color-mix(in oklch, var(--color-base-content) 10%, transparent);
  border-radius: 12px;
}

.file-chip__icon {
  display: grid;
  place-items: center;
  width: 36px;
  height: 36px;
  border-radius: 8px;
  background: color-mix(in oklch, var(--color-primary) 18%, transparent);
  color: var(--color-primary);
  flex-shrink: 0;
}

.file-chip__meta {
  flex: 1;
  min-width: 0;
}

.file-chip__name {
  font-size: 14px;
  font-weight: 600;
  color: var(--color-base-content);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.file-chip__size {
  font-family: var(--font-mono);
  font-size: 11px;
  letter-spacing: 0.04em;
  color: color-mix(in oklch, var(--color-base-content) 45%, transparent);
}

.file-chip__remove {
  display: grid;
  place-items: center;
  width: 28px;
  height: 28px;
  background: transparent;
  color: color-mix(in oklch, var(--color-base-content) 55%, transparent);
  border: 0;
  border-radius: 6px;
  cursor: pointer;
  transition: background-color 120ms ease, color 120ms ease;
  flex-shrink: 0;
}

.file-chip__remove:hover {
  background: color-mix(in oklch, var(--color-error) 14%, transparent);
  color: var(--color-error);
}
</style>
