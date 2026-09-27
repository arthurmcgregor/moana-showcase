<script setup lang="ts">
import { computed } from "vue";
import type { ScopeStats, TreeSegment } from "@/engine";
import { formatClockTime, formatDistanceNm, formatSpanDuration } from "@/utils/formatters";

// One mode segment: name and duration on a line, the duration bar beneath it.
// The bar starts in the same place on every row, which it would not if it sat
// inline after a name of varying length.
//
// No clock times. The rows are in order and each carries how long it lasted, so
// a start and an end were three ways of saying the same thing — and the pair of
// them was the widest line in a rail the legs themselves have to fit in. The
// times an eye actually hunts for are the ones on the annotations, which keep
// theirs.

const props = defineProps<{
  segment: TreeSegment;
  selected: boolean;
  /**
   * Why a click will not land, when it will not: either another leg owns this
   * segment, or reaching it would mean extending the run across one that does.
   * Dimmed to say so before the click rather than after it does nothing.
   */
  blocked?: "claimed" | "unreachable" | null;
  /**
   * Figures for this segment, given only to the one the playhead is in. Null on
   * every other row, which keeps the list a list.
   */
  stats?: ScopeStats | null;
  /** Longest segment in the journey — the shared baseline for every bar. */
  maxDurationMs: number;
}>();

const emit = defineEmits<{
  select: [];
  /** Shift-click: extend the selected run rather than replacing it. */
  rangeSelect: [];
}>();

const blockedTitle = computed(() => {
  if (props.blocked === "claimed") return "Already in another leg — remove it from that leg first";
  if (props.blocked === "unreachable") return "Not reachable — a leg must be one unbroken run";
  return undefined;
});

function onActivate(event: MouseEvent | KeyboardEvent) {
  if (props.blocked) return;
  if (event.shiftKey) emit("rangeSelect");
  else emit("select");
}

/**
 * Shift+mousedown is the browser's own extend-the-text-selection gesture, and it
 * fires long before the click we care about — so the row has to refuse it rather
 * than clean up afterwards. `select-none` on the row handles the drag case;
 * comment bodies opt text selection back in, because notes are worth copying.
 */
function onMousedown(event: MouseEvent) {
  if (event.shiftKey) event.preventDefault();
}

const barPercent = () => {
  if (!props.segment.durationMs || !props.maxDurationMs) return 0;
  return (props.segment.durationMs / props.maxDurationMs) * 100;
};
</script>

<template>
  <!-- A div rather than a button: the comment blocks below carry their own
       "Read more" button, which may not nest inside one. -->
  <div
    role="button"
    tabindex="0"
    class="w-full select-none rounded px-2 py-1 text-left transition-colors"
    :class="[
      blocked ? 'cursor-not-allowed opacity-40' : 'cursor-pointer hover:bg-base-200',
      selected ? 'bg-primary/10 ring-2 ring-primary/50' : '',
    ]"
    :aria-pressed="selected"
    :aria-disabled="!!blocked || undefined"
    :title="blockedTitle"
    @mousedown="onMousedown"
    @click="onActivate"
    @keydown.enter.prevent="onActivate"
    @keydown.space.prevent="onActivate"
  >
    <div class="flex items-center gap-2">
      <span
        class="size-2 shrink-0 rounded-full"
        :style="{ backgroundColor: segment.color }"
      />
      <span class="truncate text-sm font-medium">{{ segment.label }}</span>
      <!-- Hard right, so the durations read as a column against the rail's edge
           rather than starting wherever the mode's name happened to end. -->
      <span class="ml-auto shrink-0 font-mono text-[11px] tabular-nums text-base-content/50">
        {{ formatSpanDuration(segment.durationMs) }}
      </span>
    </div>

    <div class="mt-1 ml-4 h-1 rounded-full bg-base-300">
      <div
        class="h-full rounded-full"
        :style="{ width: barPercent() + '%', backgroundColor: segment.color }"
      />
    </div>

    <!-- What the segment under the playhead came to. Under the bar rather than
         beside the name, so the row above it stays the same shape it has on
         every other row and the extra line reads as an opening, not a reflow. -->
    <div
      v-if="stats"
      class="mt-1 ml-4 flex flex-wrap gap-x-3 font-mono text-[11px] tabular-nums text-base-content/50"
    >
      <span>{{ formatDistanceNm(stats.distanceNm) }}</span>
      <span v-if="stats.speed">
        {{ stats.speed.avg.toFixed(1) }} avg · {{ stats.speed.max.toFixed(1) }} max kts
      </span>
    </div>

    <div
      v-if="segment.chips.length"
      class="mt-1 ml-4 flex flex-wrap gap-1.5"
    >
      <span
        v-for="ann in segment.chips"
        :key="ann.id"
        class="inline-flex items-center gap-1 font-mono text-[11px] text-base-content/60"
        :title="ann.label"
      >
        <span
          class="size-2 rounded-full"
          :style="{ backgroundColor: ann.color }"
        />
        {{ ann.label }} {{ formatClockTime(ann.timestamp) }}
      </span>
    </div>

    <div
      v-if="segment.comments.length"
      class="mt-2 ml-4 select-text space-y-1"
      @click.stop
      @mousedown.stop
    >
      <AnnotationComment
        v-for="ann in segment.comments"
        :key="ann.id"
        :label="ann.type === 'NOTE' ? '' : ann.label"
        :time="formatClockTime(ann.timestamp)"
        :color="ann.color"
        :body="ann.notes"
      />
    </div>
  </div>
</template>
