<script lang="ts">
export interface DialBar {
  /** Null draws the track unlit, which is not the same as a reading of zero. */
  value: number | null;
  /** Tailwind text colour for the lit marks. */
  toneClass: string;
  /**
   * A high-water mark drawn across the track as a line: the most this bar has
   * been at some point, against what it is now. Null or zero draws nothing —
   * a mark sitting on the zero tick says the same as no mark at all, and says
   * it in a place the eye has to check.
   */
  peak?: number | null;
  /** Tailwind text colour for that line. Defaults to a bright neutral. */
  peakToneClass?: string;
}
</script>

<script setup lang="ts">
import { computed } from "vue";
import { DIAL_SIZE, polar } from "./dialGeometry";

// An instrument dial: a numbered scale ring with one or more segmented bars read
// against it, and whatever the caller wants in the middle of them.
//
// The scale is the reason for the ring. A bar with no numerals against it says
// "some" and "more"; with them it says how many knots, and two dials built on
// the same scale can be compared instead of merely looked at.
//
// Segments rather than a smooth arc for the same reason a fuel gauge is stepped:
// the eye counts marks far better than it judges the length of a curve, and the
// marks line up with the ticks that carry the numerals.
//
// Shared because the wind and the boat's speed are the same instrument twice —
// one has a second bar for the gust and a compass in the middle, the other has
// neither, and everything between the two is this file.
//
// A plain SVG on a fixed viewBox, as in `SpeedLane.vue`: no canvas, no resize
// observer, and the theme reaches it through `currentColor`.

const props = defineProps<{
  /** The top of the scale. Rounded up to a whole five by the caller. */
  max: number;
  /** Outermost first. Each takes the next track in. */
  bars: DialBar[];
}>();

/**
 * The dial is laid out on a square but does not fill one: the scale opens at the
 * bottom, so the last quarter of the box holds nothing but the two numerals
 * either side of the gap. The viewBox is cropped to where the ink actually stops
 * — the lowest numerals, plus room for their descenders — rather than leaving
 * the panel to pad empty square.
 */
const VIEW_HEIGHT = 174;

/** The scale opens at the bottom: 270° of it, with the gap under the middle. */
const START_DEG = 135;
const SWEEP_DEG = 270;

const NUMERAL_RADIUS = 92;
const TICK_OUTER = 82;
const TICK_MINOR_INNER = 76;
const TICK_MAJOR_INNER = 72;

/** The outermost bar's centre line, and how far in each next one sits. */
const FIRST_BAR_RADIUS = 64;
const BAR_PITCH = 10;
const BAR_WIDTH = 7;

/** Enough marks to read a proportion off, few enough to stay distinct at 180px. */
const SEGMENTS = 40;
const SEGMENT_PITCH = SWEEP_DEG / SEGMENTS;
const SEGMENT_GAP = 1.7;

/**
 * The finest round step that divides the scale exactly and still fits eight
 * numerals or fewer on the ring.
 *
 * Dividing exactly is what keeps the top of the scale labelled: a step of 2 over
 * a scale of 15 would number 0 through 14 and leave the last tick — the one the
 * whole dial is measured against — anonymous. Finest rather than coarsest
 * because a short scale is where the numerals are most wanted: a dial reaching
 * 10 knots numbered only 0, 5 and 10 is three marks holding up forty segments.
 */
const ROUND_STEPS = [1, 2, 5, 10, 15, 20, 25, 50, 100];

const majorStep = computed(() => {
  const fits = (step: number) => props.max / step <= 8;
  // A step that divides the scale exactly, so the top tick carries a numeral.
  for (const step of ROUND_STEPS) {
    if (props.max % step === 0 && fits(step)) return step;
  }
  // Failing that, the finest round step that still fits. The scale's top then
  // goes unlabelled, which is the ordinary way a gauge behaves and is much
  // better than numbering past it.
  for (const step of ROUND_STEPS) {
    if (fits(step)) return step;
  }
  return props.max;
});

/** Five minors to a major, which is what makes every fifth tick a numbered one. */
const MINORS_PER_MAJOR = 5;

function degForValue(value: number): number {
  const fraction = Math.min(Math.max(value / props.max, 0), 1);
  return START_DEG + SWEEP_DEG * fraction;
}

function radialPath(deg: number, innerRadius: number, outerRadius: number): string {
  const inner = polar(innerRadius, deg);
  const outer = polar(outerRadius, deg);
  return `M ${inner.x} ${inner.y} L ${outer.x} ${outer.y}`;
}

/** One mark of a bar, as a stubby arc stroked at the bar's own width. */
function segmentPath(radius: number, index: number): string {
  const from = START_DEG + index * SEGMENT_PITCH + SEGMENT_GAP / 2;
  const to = START_DEG + (index + 1) * SEGMENT_PITCH - SEGMENT_GAP / 2;
  const a = polar(radius, from);
  const b = polar(radius, to);
  return `M ${a.x} ${a.y} A ${radius} ${radius} 0 0 1 ${b.x} ${b.y}`;
}

const segmentIndices = Array.from({ length: SEGMENTS }, (_, i) => i);

/** How many marks a value lights: any reading at all is worth one. */
function litCount(value: number | null): number {
  if (value === null || value <= 0) return 0;
  return Math.min(SEGMENTS, Math.ceil((value / props.max) * SEGMENTS));
}

/**
 * The peak line, drawn radially across its own bar and a little proud of it
 * either side.
 *
 * Across rather than along: a lit segment is an arc, so a mark that were also an
 * arc would read as one more segment of the bar and not as a limit on it. A
 * radial stroke is the one direction nothing else on the ring runs in — the
 * ticks do, which is exactly why it reads as a mark on the scale.
 */
const PEAK_OVERHANG = 2;

const tracks = computed(() =>
  props.bars.map((bar, i) => {
    const radius = FIRST_BAR_RADIUS - i * BAR_PITCH;
    const peak = bar.peak ?? null;
    return {
      radius,
      lit: litCount(bar.value),
      toneClass: bar.toneClass,
      peak:
        peak === null || peak <= 0
          ? null
          : {
              d: radialPath(
                degForValue(peak),
                radius - BAR_WIDTH / 2 - PEAK_OVERHANG,
                radius + BAR_WIDTH / 2 + PEAK_OVERHANG,
              ),
              toneClass: bar.peakToneClass ?? "text-base-content/80",
            },
    };
  }),
);

// Counted in whole minor ticks rather than accumulated from a fractional step:
// a step of 0.4 summed twenty-five times lands on 9.999999999999998, which is a
// numeral nobody wants to read. Both counts floor, so neither a tick nor a
// numeral is ever drawn past the top of the scale.
const majorCount = computed(() => Math.floor(props.max / majorStep.value + 1e-9));

const minorCount = computed(() =>
  Math.floor((props.max * MINORS_PER_MAJOR) / majorStep.value + 1e-9),
);

const ticks = computed(() =>
  Array.from({ length: minorCount.value + 1 }, (_, i) => {
    const major = i % MINORS_PER_MAJOR === 0;
    const value = (i / MINORS_PER_MAJOR) * majorStep.value;
    return {
      i,
      major,
      d: radialPath(
        degForValue(value),
        major ? TICK_MAJOR_INNER : TICK_MINOR_INNER,
        TICK_OUTER,
      ),
    };
  }),
);

const numerals = computed(() =>
  Array.from({ length: majorCount.value + 1 }, (_, k) => {
    const value = k * majorStep.value;
    return { value, ...polar(NUMERAL_RADIUS, degForValue(value)) };
  }),
);
</script>

<template>
  <svg
    :viewBox="`0 0 ${DIAL_SIZE} ${VIEW_HEIGHT}`"
    class="block w-full"
    fill="none"
  >
    <!-- The scale. Minor ticks all the way round, majors reaching further out to
         carry a numeral. -->
    <g stroke="currentColor">
      <path
        v-for="tick in ticks"
        :key="`t-${tick.i}`"
        :d="tick.d"
        :class="tick.major ? 'text-base-content/40' : 'text-base-content/15'"
        :stroke-width="tick.major ? 2 : 1.5"
      />
    </g>
    <g
      class="fill-current text-base-content/45"
      font-size="10"
      text-anchor="middle"
      dominant-baseline="central"
    >
      <text
        v-for="numeral in numerals"
        :key="`n-${numeral.value}`"
        :x="numeral.x"
        :y="numeral.y"
        class="font-mono tabular-nums"
      >{{ numeral.value }}</text>
    </g>

    <!-- The bars. Every mark is drawn faint first, so the unlit ones show how
         much of the scale is left above the reading. -->
    <g
      stroke="currentColor"
      stroke-linecap="butt"
    >
      <template
        v-for="(track, b) in tracks"
        :key="`b-${b}`"
      >
        <path
          v-for="i in segmentIndices"
          :key="`b-${b}-${i}`"
          :d="segmentPath(track.radius, i)"
          :class="i < track.lit ? track.toneClass : 'text-base-content/10'"
          :stroke-width="BAR_WIDTH"
        />
        <!-- Drawn after its own bar so it is never buried by a mark the reading
             has climbed over. -->
        <path
          v-if="track.peak"
          :key="`p-${b}`"
          :d="track.peak.d"
          :class="track.peak.toneClass"
          stroke-width="2"
          stroke-linecap="round"
        />
      </template>
    </g>

    <!-- Whatever the instrument keeps in the middle: a compass and an arrow, a
         number, or nothing. Drawn in the dial's own coordinates, which
         `dialGeometry` is the one copy of. -->
    <slot />
  </svg>
</template>
