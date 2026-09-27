<script setup lang="ts">
import { ref, type Component } from "vue";

const props = defineProps<{
  accept?: string;
  primary?: string;
  secondary?: string;
  icon?: Component;
  validate?: (file: File) => boolean;
}>();

const emit = defineEmits<{
  select: [file: File];
}>();

const isDragging = ref(false);
const inputRef = ref<HTMLInputElement | null>(null);

function isAccepted(file: File) {
  if (props.validate) return props.validate(file);
  if (!props.accept) return true;
  return props.accept.split(",").some((token) => {
    const t = token.trim().toLowerCase();
    if (!t) return false;
    if (t.startsWith(".")) return file.name.toLowerCase().endsWith(t);
    if (t.endsWith("/*")) return file.type.startsWith(t.slice(0, -1));
    return file.type === t;
  });
}

function onDrop(event: DragEvent) {
  isDragging.value = false;
  const file = event.dataTransfer?.files?.[0];
  if (file && isAccepted(file)) emit("select", file);
}

function onChange(event: Event) {
  const file = (event.target as HTMLInputElement).files?.[0];
  if (file) emit("select", file);
}

function open() {
  inputRef.value?.click();
}

defineExpose({ open, reset: () => { if (inputRef.value) inputRef.value.value = ""; } });
</script>

<template>
  <div
    class="dropzone"
    :class="{ 'dropzone--active': isDragging }"
    @click="open"
    @dragenter.prevent="isDragging = true"
    @dragover.prevent="isDragging = true"
    @dragleave.prevent="isDragging = false"
    @drop.prevent="onDrop"
  >
    <component
      :is="icon"
      v-if="icon"
      class="dropzone__icon"
    />
    <OutlineArrowUpTrayIcon
      v-else
      class="dropzone__icon"
    />
    <div class="dropzone__label">
      <span class="dropzone__primary">{{ primary ?? "Drop a file here" }}</span>
      <span class="dropzone__secondary">{{ secondary ?? "or click to browse" }}</span>
    </div>
    <input
      ref="inputRef"
      type="file"
      :accept="accept"
      class="hidden"
      @change="onChange"
    >
  </div>
</template>

<style scoped>
.dropzone {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 28px 20px;
  background: color-mix(in oklch, var(--color-base-content) 2%, transparent);
  border: 1.5px dashed color-mix(in oklch, var(--color-base-content) 18%, transparent);
  border-radius: 12px;
  cursor: pointer;
  transition: border-color 160ms ease, background-color 160ms ease, transform 160ms ease;
}

.dropzone:hover,
.dropzone--active {
  border-color: color-mix(in oklch, var(--color-primary) 60%, transparent);
  background: color-mix(in oklch, var(--color-primary) 6%, transparent);
}

.dropzone--active {
  transform: scale(1.01);
}

.dropzone__icon {
  width: 30px;
  height: 30px;
  color: color-mix(in oklch, var(--color-base-content) 50%, transparent);
}

.dropzone:hover .dropzone__icon,
.dropzone--active .dropzone__icon {
  color: var(--color-primary);
}

.dropzone__label {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}

.dropzone__primary {
  font-size: 14px;
  font-weight: 600;
  color: var(--color-base-content);
}

.dropzone__secondary {
  font-size: 12px;
  color: color-mix(in oklch, var(--color-base-content) 50%, transparent);
}
</style>
