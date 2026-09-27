<script setup lang="ts">
import { computed, ref } from "vue";
import type { TreeSegment } from "@/engine";

// The permanent, dominant element of the deck: the whole day as one bar of mode
// blocks. Editing layers handles onto this bar rather than introducing a second
// one, so its geometry is the deck's geometry.

const props = defineProps<{
  segments: TreeSegment[];
  startMs: number;
  endMs: number;
  /** The selected scope, drawn at full strength while the rest recedes. */
  scopeStartMs: number;
  scopeEndMs: number;
  nothingSelected: boolean;
  /** Editing layers handles onto this bar rather than adding a second one. */
  editable?: boolean;
}>();

const emit = defineEmits<{
  seek: [epochMs: number];
  /** Drop a boundary: the segment starting here now starts at `epochMs`. */
  moveBoundary: [segmentId: number, epochMs: number];
}>();

const range = computed(() => Math.max(props.endMs - props.startMs, 1));

function percent(ms: number): number {
  return ((ms - props.startMs) / range.value) * 100;
}

interface Block {
  id: number;
  label: string;
  color: string;
  left: number;
  width: number;
  inScope: boolean;
}

const blocks = computed<Block[]>(() =>
  props.segments.map((segment) => {
    const end = segment.endMs ?? props.endMs;
    const left = percent(segment.startMs);
    const right = percent(end);
    const mid = (segment.startMs + end) / 2;
    return {
      id: segment.id,
      label: segment.label,
      color: segment.color,
      left: Math.max(0, left),
      // Never let a short segment vanish entirely.
      width: Math.max(right - left, 0.4),
      inScope: props.nothingSelected || (mid >= props.scopeStartMs && mid <= props.scopeEndMs),
    };
  }),
);

function timeAt(clientX: number, rect: DOMRect): number {
  const ratio = Math.min(Math.max((clientX - rect.left) / rect.width, 0), 1);
  return props.startMs + ratio * range.value;
}

function onClick(event: MouseEvent) {
  const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
  if (rect.width === 0) return;
  emit("seek", timeAt(event.clientX, rect));
}

// ── Boundary handles ──

// The first segment's start is the journey's own start, so it is not a boundary
// between two segments and gets no handle.
const handles = computed(() =>
  props.segments.slice(1).map((segment) => ({
    id: segment.id,
    left: percent(segment.startMs),
  })),
);

const draggingId = ref<number | null>(null);
const dragPercent = ref(0);

function startDrag(segmentId: number, event: PointerEvent) {
  if (!props.editable) return;
  const bar = (event.currentTarget as HTMLElement).parentElement;
  if (!bar) return;

  const rect = bar.getBoundingClientRect();
  if (rect.width === 0) return;

  draggingId.value = segmentId;
  dragPercent.value = ((event.clientX - rect.left) / rect.width) * 100;

  const move = (e: PointerEvent) => {
    dragPercent.value = Math.min(Math.max(((e.clientX - rect.left) / rect.width) * 100, 0), 100);
  };
  const finish = (e: PointerEvent) => {
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", finish);
    draggingId.value = null;
    emit("moveBoundary", segmentId, timeAt(e.clientX, rect));
  };

  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", finish);
}
</script>

<template>
  <div
    class="relative h-7 w-full cursor-pointer overflow-hidden rounded bg-base-300"
    @click="onClick"
  >
    <div
      v-for="block in blocks"
      :key="block.id"
      class="absolute inset-y-0 transition-opacity"
      :style="{
        left: block.left + '%',
        width: block.width + '%',
        backgroundColor: block.color,
        opacity: block.inScope ? 1 : 0.18,
      }"
      :title="block.label"
    />

    <template v-if="editable">
      <button
        v-for="handle in handles"
        :key="handle.id"
        type="button"
        class="absolute inset-y-0 z-10 w-2 -translate-x-1/2 cursor-ew-resize bg-base-content/0 hover:bg-base-content/20"
        :style="{ left: (draggingId === handle.id ? dragPercent : handle.left) + '%' }"
        title="Drag to move this boundary"
        @click.stop
        @pointerdown.stop.prevent="startDrag(handle.id, $event)"
      >
        <span class="pointer-events-none absolute inset-y-1 left-1/2 w-px -translate-x-1/2 bg-base-100/80" />
        <span class="sr-only">Move boundary</span>
      </button>
    </template>
  </div>
</template>
