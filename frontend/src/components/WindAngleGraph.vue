<script setup lang="ts">
import { Scatter, Bubble } from "vue-chartjs";
import { computed, ref } from "vue";
import type { GeoJSONFeature, GeoJSONFeatureCollection, JourneyFull } from "@/api/web";
import { useChart } from "@/composables/useChart";

const props = defineProps<{
  journeyTracks: GeoJSONFeatureCollection;
  journeyFull: JourneyFull;
}>();

const sortedSailingIntervals = computed(() => {
  if (!props.journeyFull.modes?.length) return [];
  return props.journeyFull.modes
    .filter((m) => m.mode === "SAIL")
    .map((m) => ({
      start: new Date(m.started_at).getTime(),
      end: m.ended_at ? new Date(m.ended_at).getTime() : Infinity,
    }))
    .sort((a, b) => a.start - b.start);
});

function isSailing(timeMs: number): boolean {
  return sortedSailingIntervals.value.some((iv) => timeMs >= iv.start && timeMs <= iv.end);
}

// Wind direction: direction the wind is coming FROM (degrees, 0=N, 90=E)
const windDirection = ref(90);
const viewMode = ref<"scatter" | "density">("density");

// True when at least one track point has weather-joined wind_direction data
const hasWeatherData = computed(() =>
  props.journeyTracks.features.some((f) => f.properties?.wind_direction != null),
);

/**
 * The wind to plot this fix against, or null if there is none.
 *
 * Once any point carries station data the chart says so and hides the manual
 * dial, so falling back to that dial's value for the points the weather join
 * missed would quietly plot them against a fixed easterly and label the result
 * station data. A fix with no wind behind it is not a measurement, so it is
 * dropped and counted rather than invented.
 */
function windDirFor(feature: GeoJSONFeature): number | null {
  const joined = feature.properties?.wind_direction;
  if (joined != null) return Number(joined);
  return hasWeatherData.value ? null : windDirection.value;
}

// Compute signed true wind angle: positive = starboard, negative = port (-180..180)
function computeWindAngle(course: number, windDir: number): number {
  let twa = ((windDir - course) % 360 + 360) % 360;
  if (twa > 180) twa -= 360;
  return twa;
}

/**
 * Below this the station's wind direction is not a measurement of anything the
 * boat could sail to. Measured against a minute-by-minute record: at 8-9 kn the
 * direction moves 3 degrees a minute and at 1-2 kn it moves 23, with a ninetieth
 * percentile of 74 and excursions past 150. A true wind angle inherits every one
 * of those degrees, so light-air fixes smear across the whole polar — including
 * the no-go zone no boat can sail in.
 *
 * Three knots is where that curve flattens: it holds 86% of a day's readings and
 * takes the ninetieth percentile from 20 degrees to 15, where four knots costs a
 * further 6% of the day to win two more.
 */
const MIN_WIND_KN = 3;

/**
 * Every fix the polar can honestly plot: sailing, moving, and with a wind behind
 * it worth believing. Both charts read this rather than filtering the features
 * themselves — the rule has four parts now, and the scatter and the density have
 * to agree about all of them.
 */
const plottable = computed(() => {
  const fixes: { twa: number; speed: number }[] = [];
  let noWind = 0;
  let lightAir = 0;

  for (const feature of props.journeyTracks.features) {
    const course = feature.properties?.course;
    const speed = feature.properties?.speed;
    const timestamp = feature.properties?.timestamp;
    if (course == null || speed == null || speed <= 0) continue;
    if (!timestamp || !isSailing(new Date(timestamp).getTime())) continue;

    const windDir = windDirFor(feature);
    if (windDir == null) {
      noWind++;
      continue;
    }

    // Only when the station says so. A hand-set direction carries no wind speed
    // to judge, and a joined direction without one is not evidence of calm.
    const windKn = feature.properties?.wind_speed;
    if (windKn != null && Number(windKn) < MIN_WIND_KN) {
      lightAir++;
      continue;
    }

    fixes.push({ twa: computeWindAngle(Number(course), windDir), speed: Number(speed) });
  }

  return { fixes, noWind, lightAir };
});

/** Why the polar is not showing every sailing fix, when it is not. */
const exclusionNote = computed(() => {
  const { noWind, lightAir } = plottable.value;
  const parts: string[] = [];
  if (lightAir) parts.push(`${lightAir} under ${MIN_WIND_KN} kn`);
  if (noWind) parts.push(`${noWind} without wind data`);
  return parts.length ? `Not plotted: ${parts.join(", ")}` : "";
});

const chartData = computed(() => {
  const starboard: { x: number; y: number }[] = [];
  const port: { x: number; y: number }[] = [];

  for (const { twa, speed } of plottable.value.fixes) {
    const point = { x: twa, y: speed };
    if (twa >= 0) {
      starboard.push(point);
    } else {
      port.push(point);
    }
  }

  return {
    datasets: [
      {
        label: "Port",
        data: port,
        backgroundColor: "rgba(239, 68, 68, 0.6)",
        pointRadius: 3,
        pointHoverRadius: 5,
      },
      {
        label: "Starboard",
        data: starboard,
        backgroundColor: "rgba(34, 197, 94, 0.6)",
        pointRadius: 3,
        pointHoverRadius: 5,
      },
    ],
  };
});

const { buildOptions } = useChart();

const chartOptions = buildOptions<"scatter">((colors) => ({
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { display: true, labels: { color: colors.axis } },
    title: {
      display: true,
      text: hasWeatherData.value
        ? "Speed vs Wind Angle (per-point weather data)"
        : `Speed vs Wind Angle (wind from ${windDirection.value}°)`,
      color: colors.label,
    },
    tooltip: { enabled: false },
    annotation: { annotations: sailZoneAnnotations },
  },
  scales: {
    x: {
      min: -180,
      max: 180,
      title: { display: true, text: "Wind Angle (° — port ← 0 → starboard)", color: colors.label },
      ticks: {
        stepSize: 30,
        callback: (val) => `${val}°`,
        color: colors.axis,
      },
      grid: { color: colors.grid },
    },
    y: {
      beginAtZero: true,
      title: { display: true, text: "Speed (knots)", color: colors.label },
      ticks: { color: colors.axis },
      grid: { color: colors.grid },
    },
  },
}));

// Points of sail annotations: no-go zone box, boundary lines, labels on both sides
// Boundaries: no-go ±0–30°, close hauled ±30–45°, reaching ±45–90°, broad reach ±90–135°, running ±135–180°
const sailZoneAnnotations = Object.fromEntries([
  // No-go zone shaded box
  [
    "noGo",
    {
      type: "box" as const,
      xMin: -30,
      xMax: 30,
      backgroundColor: "rgba(150,150,150,0.12)",
      borderColor: "rgba(150,150,150,0.35)",
      borderWidth: 1,
      label: {
        display: true,
        content: "No-go",
        position: { x: "center", y: "start" } as const,
        backgroundColor: "transparent",
        color: "rgba(120,120,120,0.7)",
        font: { size: 9 },
        yAdjust: 5,
      },
    },
  ],
  // Dashed boundary lines (symmetric) at ±30, ±45, ±90, ±135
  ...[-135, -90, -45, -30, 30, 45, 90, 135].map((v, i) => [
    `boundary${i}`,
    {
      type: "line" as const,
      scaleID: "x",
      value: v,
      borderColor: "rgba(150,150,150,0.35)",
      borderWidth: 1,
      borderDash: [4, 3],
    },
  ]),
  // Region labels on both sides, horizontal, near top
  ...[
    { x: 37.5, text: "Close hauled" },
    { x: 67.5, text: "Reaching" },
    { x: 112.5, text: "Broad reach" },
    { x: 157.5, text: "Running" },
    { x: -37.5, text: "Close hauled" },
    { x: -67.5, text: "Reaching" },
    { x: -112.5, text: "Broad reach" },
    { x: -157.5, text: "Running" },
  ].map((z, i) => [
    `label${i}`,
    {
      type: "line" as const,
      scaleID: "x",
      value: z.x,
      borderColor: "transparent",
      borderWidth: 0,
      label: {
        display: true,
        content: z.text,
        position: "start" as const,
        backgroundColor: "transparent",
        color: "rgba(120,120,120,0.75)",
        font: { size: 9 },
        yAdjust: 5,
      },
    },
  ]),
]);

const ANGLE_BIN_SIZE = 10; // degrees
const SPEED_BIN_SIZE = 0.5; // knots

const densityData = computed(() => {
  // bins: Map key = `${angleBin},${speedBin}` → count
  const starboardBins = new Map<string, { x: number; y: number; count: number }>();
  const portBins = new Map<string, { x: number; y: number; count: number }>();
  let maxSpeed = 0;

  for (const { twa, speed: spd } of plottable.value.fixes) {
    if (spd > maxSpeed) maxSpeed = spd;

    const angleBin = Math.floor(twa / ANGLE_BIN_SIZE) * ANGLE_BIN_SIZE;
    const speedBin = Math.floor(spd / SPEED_BIN_SIZE) * SPEED_BIN_SIZE;
    const key = `${angleBin},${speedBin}`;
    const cx = angleBin + ANGLE_BIN_SIZE / 2;
    const cy = speedBin + SPEED_BIN_SIZE / 2;

    const bins = twa >= 0 ? starboardBins : portBins;
    const existing = bins.get(key);
    if (existing) {
      existing.count++;
    } else {
      bins.set(key, { x: cx, y: cy, count: 1 });
    }
  }

  const toBubbles = (bins: Map<string, { x: number; y: number; count: number }>) =>
    Array.from(bins.values()).map(({ x, y, count }) => ({
      x,
      y,
      r: Math.min(Math.sqrt(count) * 3, 20),
      count,
    }));

  return {
    maxSpeed,
    datasets: [
      {
        label: "Port",
        data: toBubbles(portBins),
        backgroundColor: "rgba(239, 68, 68, 0.6)",
      },
      {
        label: "Starboard",
        data: toBubbles(starboardBins),
        backgroundColor: "rgba(34, 197, 94, 0.6)",
      },
    ],
  };
});

const densityOptions = buildOptions<"bubble">((colors) => ({
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { display: true, labels: { color: colors.axis } },
    title: {
      display: true,
      text: hasWeatherData.value
        ? "Speed vs Wind Angle — Density (per-point weather data)"
        : `Speed vs Wind Angle — Density (wind from ${windDirection.value}°)`,
      color: colors.label,
    },
    tooltip: {
      callbacks: {
        label: (ctx) => {
          const { x, y, count } = ctx.raw as { x: number; y: number; r: number; count: number };
          const aLo = (x - ANGLE_BIN_SIZE / 2).toFixed(0);
          const aHi = (x + ANGLE_BIN_SIZE / 2).toFixed(0);
          const sLo = (y - SPEED_BIN_SIZE / 2).toFixed(1);
          const sHi = (y + SPEED_BIN_SIZE / 2).toFixed(1);
          return `${aLo}° to ${aHi}° | ${sLo}–${sHi} kn | ${count} pts`;
        },
      },
    },
    annotation: { annotations: sailZoneAnnotations },
  },
  scales: {
    x: {
      min: -180,
      max: 180,
      title: { display: true, text: "Wind Angle (° — port ← 0 → starboard)", color: colors.label },
      ticks: {
        stepSize: 30,
        callback: (val) => `${val}°`,
        color: colors.axis,
      },
      grid: { color: colors.grid },
    },
    y: {
      beginAtZero: true,
      max: Math.ceil((densityData.value.maxSpeed + 1) / 2) * 2,
      title: { display: true, text: "Speed (knots)", color: colors.label },
      ticks: { color: colors.axis },
      grid: { color: colors.grid },
    },
  },
}));
</script>

<template>
  <div class="flex h-full flex-col gap-3">
    <div class="flex flex-wrap items-center gap-3">
      <template v-if="!hasWeatherData">
        <label class="text-sm font-medium">Wind direction (from):</label>
        <input
          v-model.number="windDirection"
          type="number"
          min="0"
          max="359"
          class="input input-sm input-bordered w-24"
        >
        <span class="text-sm text-base-content/60">° (0=N, 90=E, 180=S, 270=W)</span>
      </template>
      <span
        v-else
        class="text-sm text-base-content/60"
      >Using weather station wind data</span>
      <span
        v-if="exclusionNote"
        class="text-sm text-warning/80"
        :title="`In under ${MIN_WIND_KN} kn the station's wind direction wanders by tens of degrees a minute, so the angle it gives is not one the boat could sail to. Fixes with no observation within five minutes are dropped rather than plotted against a guess.`"
      >{{ exclusionNote }}</span>
      <div class="join ml-auto">
        <button
          class="join-item btn btn-sm"
          :class="viewMode === 'density' ? 'btn-primary' : 'btn-ghost'"
          @click="viewMode = 'density'"
        >
          Density
        </button>
        <button
          class="join-item btn btn-sm"
          :class="viewMode === 'scatter' ? 'btn-primary' : 'btn-ghost'"
          @click="viewMode = 'scatter'"
        >
          Scatter
        </button>
      </div>
    </div>
    <div
      v-if="chartData.datasets[0].data.length > 0 || chartData.datasets[1].data.length > 0"
      class="min-h-0 flex-1"
    >
      <Bubble
        v-if="viewMode === 'density'"
        :data="densityData"
        :options="densityOptions"
      />
      <Scatter
        v-else
        :data="chartData"
        :options="chartOptions"
      />
    </div>
    <div
      v-else
      class="flex flex-1 items-center justify-center text-base-content/50"
    >
      No sailing points with course data available.
    </div>
  </div>
</template>
