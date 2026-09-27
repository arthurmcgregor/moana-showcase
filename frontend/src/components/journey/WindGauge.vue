<script setup lang="ts">
import { computed } from "vue";
import type { WeatherWindowStats } from "@/engine";
import { compassPoint } from "@/utils/formatters";
import SegmentedDial, { type DialBar } from "./SegmentedDial.vue";
import { CENTRE, polar } from "./dialGeometry";

// Wind as an instrument dial rather than two flat tiles: two segmented bars on a
// numbered scale — the wind and the gust it reached — and a compass inside them
// carrying the arrow the wind was blowing along.
//
// The scale, the ticks and the bars are `SegmentedDial`, which the boat's own
// speed reads off too. What is here is what makes this one the wind: the second
// bar, the compass, and the arrow.
//
// The bars share the day's scale rather than each leg's own, so a quiet leg
// looks quiet beside the day it sat in. The wind takes the outer track because
// it is the figure being read; the gust sits inside it and is read as an
// overshoot of the bar above rather than as a second reading in its own right.
//
// One moment, not two: the bars read the playhead, and nothing else is marked on
// them. The scope's average was drawn here as a notch and taken out again — a
// second mark on the same scale wants a legend to say which is which, and a
// legend is the thing the dial exists to do without. With no reading at the
// playhead the average takes the bars back, which is the only time it shows.

const props = withDefaults(
  defineProps<{
    weather: WeatherWindowStats;
    /** The day's strongest gust — what the scale is built to reach. */
    dayGustMax: number | null;
    /** The station's reading at the playhead, when it has one. */
    liveWind?: number | null;
    liveGust?: number | null;
    liveDirectionDeg?: number | null;
  }>(),
  { liveWind: null, liveGust: null, liveDirectionDeg: null },
);

const COMPASS_RADIUS = 44;
const CARDINAL_RADIUS = 35;
const ARROW_REACH = 28;

/** Rounded up to a whole 5 knots so the scale is a number you can read off. */
const scaleMax = computed(() => {
  const candidates = [
    props.dayGustMax ?? 0,
    props.weather.gustMax ?? 0,
    props.weather.wind?.max ?? 0,
    props.liveWind ?? 0,
    props.liveGust ?? 0,
  ];
  return Math.max(5, Math.ceil(Math.max(...candidates) / 5) * 5);
});

/** What the dial reads: the playhead's wind, or the scope's average without one. */
const dialWind = computed(() => props.liveWind ?? props.weather.wind?.avg ?? null);

/** The gust ring reads the same way, live first. */
const dialGust = computed(() => props.liveGust ?? props.weather.gustMax);

const bars = computed<DialBar[]>(() => [
  { value: dialWind.value, toneClass: props.liveWind !== null ? "text-accent" : "text-primary" },
  { value: dialGust.value, toneClass: "text-accent/70" },
]);

// The compass is drawn in bearings; the dial is drawn in `polar` degrees, which
// put 0 to the right. North is a quarter turn back from there.
const CARDINALS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"].map((label, i) => ({
  label,
  ...polar(CARDINAL_RADIUS, i * 45 - 90),
  cardinal: i % 2 === 0,
}));

const directionDeg = computed(
  () => props.liveDirectionDeg ?? props.weather.wind?.directionDeg ?? null,
);

// Meteorological convention: the direction is where the wind came FROM, so the
// arrow is turned to point the way it was going. Drawn pointing north, and
// `rotate` in an SVG with y downwards turns clockwise — which is the way
// bearings run, so the bearing is the rotation.
const arrowRotation = computed(() =>
  directionDeg.value === null ? null : (directionDeg.value + 180) % 360,
);

const compass = computed(() => compassPoint(directionDeg.value));

const hasAnything = computed(() => dialWind.value !== null || dialGust.value !== null);

/**
 * The dial's own sentence, for a reader who is not looking at it. The panel is
 * click-through, so a `title` never gets hovered — this is the readout.
 */
const description = computed(() => {
  if (!hasAnything.value) return "No wind reading";
  const parts: string[] = [];
  if (dialWind.value !== null) {
    parts.push(
      props.liveWind !== null
        ? `Wind ${dialWind.value.toFixed(1)} knots at the playhead`
        : `Wind averaging ${dialWind.value.toFixed(1)} knots`,
    );
  }
  if (dialGust.value !== null) parts.push(`gusting ${dialGust.value.toFixed(1)}`);
  if (compass.value) parts.push(`from the ${compass.value}`);
  parts.push(`on a scale to ${scaleMax.value}`);
  return `${parts.join(", ")}.`;
});
</script>

<template>
  <div
    v-if="hasAnything"
    role="img"
    :aria-label="description"
  >
    <SegmentedDial
      :max="scaleMax"
      :bars="bars"
    >
      <!-- The compass the arrow turns inside. Its band closes the circle the
           scale leaves open at the bottom. -->
      <circle
        :cx="CENTRE"
        :cy="CENTRE"
        :r="COMPASS_RADIUS"
        class="text-base-content/10"
        stroke="currentColor"
        stroke-width="5"
      />
      <g
        class="fill-current"
        font-size="8"
        text-anchor="middle"
        dominant-baseline="central"
      >
        <text
          v-for="point in CARDINALS"
          :key="point.label"
          :x="point.x"
          :y="point.y"
          class="font-mono"
          :class="point.cardinal ? 'text-base-content/45' : 'text-base-content/25'"
        >
          {{ point.label }}
        </text>
      </g>

      <!-- One head and a bare tail. A barb at the blunt end reads as a second
           arrowhead pointing the other way, which leaves the one thing the arrow
           exists to say — which way the wind was going — an open question. -->
      <g
        v-if="arrowRotation !== null"
        class="text-primary"
        :transform="`rotate(${arrowRotation} ${CENTRE} ${CENTRE})`"
      >
        <path
          :d="`M ${CENTRE} ${CENTRE - ARROW_REACH + 4} L ${CENTRE} ${CENTRE + ARROW_REACH}`"
          stroke="currentColor"
          stroke-width="2.5"
          stroke-linecap="round"
        />
        <path
          :d="`M ${CENTRE} ${CENTRE - ARROW_REACH}
               L ${CENTRE - 7} ${CENTRE - ARROW_REACH + 15}
               L ${CENTRE} ${CENTRE - ARROW_REACH + 10}
               L ${CENTRE + 7} ${CENTRE - ARROW_REACH + 15} Z`"
          class="fill-current"
        />
      </g>
      <circle
        :cx="CENTRE"
        :cy="CENTRE"
        r="4.5"
        class="fill-current text-base-content/35"
      />
    </SegmentedDial>
  </div>
</template>
