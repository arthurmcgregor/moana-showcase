<script setup lang="ts">
import { computed } from "vue";
import SegmentedDial, { type DialBar } from "./SegmentedDial.vue";
import { CENTRE } from "./dialGeometry";

// The boat's own speed, as the same instrument the wind gets.
//
// It was a number, and a number was the wrong shape for it: the fixes arrive
// about once a second and the figure changes on every one of them, so during a
// replay at 300× it is a blur of digits that cannot be read and cannot be
// ignored. A needle's worth of movement carries the same reading without asking
// the eye to re-read four characters thirty times a second — a bar that is a
// third full says "slow" at a glance, which is the question being asked.
//
// One bar, no compass: a boat's speed has no direction of its own worth drawing
// here, the track on the map is already saying which way it went. The scope's
// top speed sets the scale rather than the day's, so a slow leg fills the dial
// no further than it deserves.

const props = defineProps<{
  /** The reading at the playhead, or null in a recording silence. */
  speed: number | null;
  /** The fastest the boat went over the scope — the top of the dial. */
  scopeMax: number | null;
  /**
   * The fastest it had gone by the playhead — the line above the bar. It climbs
   * with the replay and never falls while it runs, which is what makes the bar
   * mean something: a reading is fast or slow against the best of the day so
   * far, not against a scale whose top the boat may never have come near.
   */
  peak?: number | null;
}>();

/** Rounded up to a whole 5 knots so the scale is a number you can read off. */
const scaleMax = computed(() =>
  Math.max(5, Math.ceil(Math.max(props.scopeMax ?? 0, props.speed ?? 0) / 5) * 5),
);

const bars = computed<DialBar[]>(() => [
  { value: props.speed, toneClass: "text-primary", peak: props.peak ?? null },
]);

const description = computed(() => {
  const peak
    = props.peak != null && props.peak > 0 ? `, best so far ${props.peak.toFixed(1)}` : "";
  return props.speed === null
    ? `No speed reading at the playhead${peak}.`
    : `${props.speed.toFixed(1)} knots${peak}, on a scale to ${scaleMax.value}.`;
});
</script>

<template>
  <div
    role="img"
    :aria-label="description"
  >
    <SegmentedDial
      :max="scaleMax"
      :bars="bars"
    >
      <!-- The figure keeps its place in the middle. The bar is what you read
           while it is moving; the number is what you read once it stops. -->
      <text
        :x="CENTRE"
        y="96"
        class="fill-current font-display tabular-nums"
        font-size="30"
        text-anchor="middle"
        dominant-baseline="central"
      >
        {{ speed === null ? "—" : speed.toFixed(1) }}
      </text>
      <text
        :x="CENTRE"
        y="119"
        class="fill-current font-mono uppercase text-base-content/40"
        font-size="9"
        text-anchor="middle"
        dominant-baseline="central"
        letter-spacing="1.5"
      >
        kts
      </text>
    </SegmentedDial>
  </div>
</template>
