<script setup lang="ts">
import { computed } from "vue";
import type { GeoJSONFeature } from "@/api/web";
import { findGaps, gapThresholdMs, splitAtGaps } from "@/utils/sampleGaps";

// Speed directly under the mode bar, on exactly the same axis — the shape is
// how you find the moment motoring became sailing. A plain SVG on a fixed
// viewBox with preserveAspectRatio="none" is what makes the axes line up; a
// canvas chart with its own padding would not.

const VIEW_WIDTH = 1000;
const VIEW_HEIGHT = 100;

const props = defineProps<{
  features: GeoJSONFeature[];
  startMs: number;
  endMs: number;
  scopeStartMs: number;
  scopeEndMs: number;
  nothingSelected: boolean;
}>();

const emit = defineEmits<{ seek: [epochMs: number] }>();

const range = computed(() => Math.max(props.endMs - props.startMs, 1));

// The whole journey's maximum, not the visible stretch's — the lane's height
// must mean the same thing at every zoom, or zooming in would look like speeding
// up.
const maxSpeed = computed(() => {
  let max = 0;
  for (const f of props.features) {
    const speed = f.properties.speed;
    if (typeof speed === "number" && speed > max) max = speed;
  }
  return max || 1;
});

interface LanePoint {
  x: number;
  y: number;
  ms: number;
}

/**
 * Parsed once per track rather than once per redraw. The lane is re-columned
 * every frame while replay slides a zoomed view along, and parsing tens of
 * thousands of timestamps at sixty frames a second is the one thing here that
 * would not keep up.
 */
const samples = computed(() => {
  const out: { ms: number; speed: number }[] = [];
  for (const f of props.features) {
    const speed = f.properties.speed;
    if (typeof speed !== "number" || Number.isNaN(speed)) continue;
    const ms = new Date(f.properties.timestamp).getTime();
    if (Number.isFinite(ms)) out.push({ ms, speed });
  }
  return out;
});

// A day of one-second fixes is tens of thousands of points for a lane a few
// hundred pixels wide. Keep the fastest fix in each column: peaks are the whole
// point of the shape, and an average would sand them off.
const points = computed<LanePoint[]>(() => {
  const columns = new Map<number, { speed: number; ms: number }>();

  for (const { ms, speed } of samples.value) {
    const column = Math.round((((ms - props.startMs) / range.value) * VIEW_WIDTH) * 2);
    // Off the ends of a zoomed view; nothing to draw and no column to hold it.
    if (column < -2 || column > VIEW_WIDTH * 2 + 2) continue;
    const existing = columns.get(column);
    if (!existing || speed > existing.speed) columns.set(column, { speed, ms });
  }

  return [...columns.entries()]
    .sort((a, b) => a[0] - b[0])
    .map(([column, { speed, ms }]) => ({
      x: column / 2,
      y: VIEW_HEIGHT - (speed / maxSpeed.value) * VIEW_HEIGHT,
      ms,
    }));
});

// Where the recording stopped. Found on the raw fixes rather than on the drawn
// points, because the columns above are themselves minutes wide on a long day.
const gaps = computed(() => {
  const sorted = samples.value.map((s) => s.ms).sort((a, b) => a - b);
  return findGaps(sorted, gapThresholdMs(sorted));
});

// One trace per stretch that was actually recorded. Drawing a single trace
// across a dropout invents the speed it crosses — a straight line reads as a
// steady passage rather than as no passage observed at all.
const runs = computed(() => splitAtGaps(points.value, (p) => p.ms, gaps.value));

const xAt = (ms: number) => ((ms - props.startMs) / range.value) * VIEW_WIDTH;

const traces = computed(() =>
  runs.value.map((pts) => ({
    line: pts.map((p) => `${p.x.toFixed(2)},${p.y.toFixed(2)}`).join(" "),
    // A fix marooned between two dropouts is a run of one, and a polyline of one
    // point draws nothing. It was on the trace before the breaks existed, so it
    // stands as a tick rather than disappearing.
    lone: pts.length === 1 ? pts[0] : null,
    area:
      pts.length < 2
        ? ""
        : `M ${pts[0].x.toFixed(2)},${VIEW_HEIGHT} ${pts
            .map((p) => `L ${p.x.toFixed(2)},${p.y.toFixed(2)}`)
            .join(" ")} L ${pts[pts.length - 1].x.toFixed(2)},${VIEW_HEIGHT} Z`,
  })),
);

// A break alone could be read as the trace having ended, so the missing stretch
// is tinted: the lane says nothing was recorded here, not that nothing happened.
const gapBands = computed(() =>
  gaps.value.map((g) => {
    const left = xAt(g.startMs);
    return { x: left, width: Math.max(xAt(g.endMs) - left, 0.5) };
  }),
);

// The out-of-scope stretches are drawn over, rather than the in-scope stretch
// drawn separately: one trace, one flat dimming floor, same rule as the map.
const scopeLeft = computed(() => ((props.scopeStartMs - props.startMs) / range.value) * VIEW_WIDTH);
const scopeRight = computed(() =>
  ((Math.min(props.scopeEndMs, props.endMs) - props.startMs) / range.value) * VIEW_WIDTH,
);

function onClick(event: MouseEvent) {
  const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
  if (rect.width === 0) return;
  const ratio = Math.min(Math.max((event.clientX - rect.left) / rect.width, 0), 1);
  emit("seek", props.startMs + ratio * range.value);
}
</script>

<template>
  <div
    class="relative h-12 w-full cursor-pointer"
    @click="onClick"
  >
    <svg
      class="h-full w-full"
      :viewBox="`0 0 ${VIEW_WIDTH} ${VIEW_HEIGHT}`"
      preserveAspectRatio="none"
      aria-hidden="true"
    >
      <rect
        v-for="(band, i) in gapBands"
        :key="`gap-${i}`"
        :x="band.x"
        y="0"
        :width="band.width"
        :height="VIEW_HEIGHT"
        class="fill-base-content/[0.07]"
      />

      <template
        v-for="(trace, i) in traces"
        :key="`trace-${i}`"
      >
        <path
          v-if="trace.area"
          :d="trace.area"
          class="fill-primary/15"
        />
        <polyline
          v-if="trace.line"
          :points="trace.line"
          class="fill-none stroke-primary"
          stroke-width="1.5"
          vector-effect="non-scaling-stroke"
        />
        <line
          v-if="trace.lone"
          :x1="trace.lone.x"
          :x2="trace.lone.x"
          :y1="trace.lone.y"
          :y2="VIEW_HEIGHT"
          class="stroke-primary"
          stroke-width="1.5"
          vector-effect="non-scaling-stroke"
        />
      </template>

      <template v-if="!nothingSelected">
        <rect
          v-if="scopeLeft > 0"
          x="0"
          y="0"
          :width="scopeLeft"
          :height="VIEW_HEIGHT"
          class="fill-base-100/80"
        />
        <rect
          v-if="scopeRight < VIEW_WIDTH"
          :x="scopeRight"
          y="0"
          :width="VIEW_WIDTH - scopeRight"
          :height="VIEW_HEIGHT"
          class="fill-base-100/80"
        />
      </template>
    </svg>

    <span class="pointer-events-none absolute left-1 top-0 font-mono text-[10px] tabular-nums text-base-content/35">
      {{ maxSpeed.toFixed(0) }} kts
    </span>
  </div>
</template>
