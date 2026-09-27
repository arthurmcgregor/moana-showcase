<script setup lang="ts">
import { computed, ref } from "vue";

const props = defineProps<{
  label: string;
  time: string;
  color: string;
  body: string;
}>();

const expanded = ref(false);

// Heuristic: long enough that line-clamp-2 is likely truncating.
// Cheap and stable vs. DOM measurement across responsive widths.
const canTruncate = computed(() => props.body.length > 140 || props.body.includes("\n"));

function toggle() {
  if (canTruncate.value) expanded.value = !expanded.value;
}
</script>

<template>
  <div class="annotation-comment">
    <span
      class="annotation-comment__rule"
      :style="{ backgroundColor: color }"
    />
    <div class="annotation-comment__content">
      <div class="annotation-comment__meta">
        <span class="annotation-comment__time">{{ time }}</span>
        <template v-if="label">
          <span class="annotation-comment__sep">·</span>
          <span class="annotation-comment__label">{{ label }}</span>
        </template>
      </div>
      <p
        class="annotation-comment__body"
        :class="{ 'annotation-comment__body--clamped': canTruncate && !expanded }"
        @click="toggle"
      >
        {{ body }}
      </p>
      <button
        v-if="canTruncate"
        type="button"
        class="annotation-comment__toggle"
        @click="toggle"
      >
        {{ expanded ? "Show less" : "Read more" }}
      </button>
    </div>
  </div>
</template>

<style scoped>
.annotation-comment {
  display: flex;
  gap: 10px;
  padding: 4px 0;
}

.annotation-comment__rule {
  width: 2px;
  flex-shrink: 0;
  border-radius: 2px;
  opacity: 0.55;
}

.annotation-comment__content {
  flex: 1;
  min-width: 0;
}

.annotation-comment__meta {
  display: flex;
  align-items: center;
  gap: 6px;
  font-family: var(--font-mono);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: color-mix(in oklch, var(--color-base-content) 45%, transparent);
}

.annotation-comment__time {
  font-variant-numeric: tabular-nums;
}

.annotation-comment__sep {
  opacity: 0.5;
}

.annotation-comment__body {
  margin-top: 3px;
  font-size: 13.5px;
  line-height: 1.55;
  color: color-mix(in oklch, var(--color-base-content) 88%, transparent);
  white-space: pre-wrap;
  cursor: default;
}

.annotation-comment__body--clamped {
  display: -webkit-box;
  -webkit-line-clamp: 2;
  line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
  cursor: pointer;
}

.annotation-comment__toggle {
  margin-top: 2px;
  font-family: var(--font-mono);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--color-primary);
  cursor: pointer;
  background: none;
  border: none;
  padding: 0;
}

.annotation-comment__toggle:hover {
  color: color-mix(in oklch, var(--color-primary) 80%, white);
}
</style>
