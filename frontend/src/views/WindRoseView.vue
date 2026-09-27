<script setup lang="ts">
import { ref, computed } from "vue";
import type { WindBin, SpeedBand } from "@/components/WindRoseChart.vue";

// 16 compass direction bins (22.5° each), labelled by centre angle
const DIR_LABELS = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
const DIR_ANGLES = [0,22.5,45,67.5,90,112.5,135,157.5,180,202.5,225,247.5,270,292.5,315,337.5];

// Band edges live in tools/wind/aggregate.py, which bins the NIWA observations into
// wind-rose.json — the licence allows publishing these counts but not the raw data.
const SPEED_BANDS: SpeedBand[] = [
  { label: "< 5 kn",   color: "#bfdbfe" },
  { label: "5–10 kn",  color: "#6ee7b7" },
  { label: "10–15 kn", color: "#fde68a" },
  { label: "15–20 kn", color: "#fdba74" },
  { label: "20–25 kn", color: "#f87171" },
  { label: "25+ kn",   color: "#991b1b" },
];

const MONTH_NAMES = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];

interface WindRose {
  source: string;
  months: Record<string, number[][]>; // "01"–"12" → [direction][speed band] counts
}

const loading = ref(true);
const error = ref<string | null>(null);
const data = ref<WindRose | null>(null);
const selectedMonth = ref("all");

fetch("/wind-rose.json")
  .then((r) => {
    if (!r.ok) throw new Error(`wind-rose.json: HTTP ${r.status}`);
    return r.json();
  })
  .then((json: WindRose) => (data.value = json))
  .catch((err) => (error.value = String(err)))
  .finally(() => (loading.value = false));

const availableMonths = computed(() =>
  data.value ? Object.keys(data.value.months).sort() : []
);

const activeCounts = computed<number[][]>(() => {
  if (!data.value) return [];
  const months = Object.values(data.value.months);
  if (selectedMonth.value !== "all") return data.value.months[selectedMonth.value] ?? [];
  return DIR_LABELS.map((_, dir) =>
    SPEED_BANDS.map((_, band) => months.reduce((sum, m) => sum + m[dir][band], 0))
  );
});

const activeBins = computed<WindBin[]>(() =>
  activeCounts.value.map((bands, i) => ({
    label: DIR_LABELS[i],
    angle: DIR_ANGLES[i],
    bands,
    total: bands.reduce((a, b) => a + b, 0),
  }))
);

const activeTotalObs = computed(() => activeBins.value.reduce((a, b) => a + b.total, 0));
</script>

<template>
  <div class="flex h-full flex-col gap-4 p-6">
    <!-- Controls -->
    <div class="flex flex-wrap items-center gap-3">
      <h2 class="font-display text-2xl font-normal tracking-tight text-base-content">
        Wind Rose — Diamond Harbour
      </h2>
      <div class="flex flex-wrap gap-1">
        <button
          class="btn btn-xs"
          :class="selectedMonth === 'all' ? 'btn-primary' : 'btn-ghost'"
          @click="selectedMonth = 'all'"
        >
          All
        </button>
        <button
          v-for="m in availableMonths"
          :key="m"
          class="btn btn-xs"
          :class="selectedMonth === m ? 'btn-primary' : 'btn-ghost'"
          @click="selectedMonth = m"
        >
          {{ MONTH_NAMES[parseInt(m) - 1] }}
        </button>
      </div>
      <span
        v-if="activeTotalObs"
        class="ml-auto text-xs text-base-content/50"
      >
        {{ activeTotalObs.toLocaleString() }} observations
      </span>
    </div>

    <!-- States -->
    <div
      v-if="loading"
      class="flex flex-1 items-center justify-center"
    >
      <span class="loading loading-spinner loading-lg" />
    </div>
    <div
      v-else-if="error"
      class="alert alert-error"
    >
      {{ error }}
    </div>

    <!-- Chart + legend -->
    <div
      v-else
      class="flex min-h-0 flex-1 flex-col gap-4 lg:flex-row lg:gap-6"
    >
      <WindRoseChart
        :bins="activeBins"
        :speed-bands="SPEED_BANDS"
        :total-obs="activeTotalObs"
        class="aspect-square max-h-[60vh] w-full min-h-0 lg:aspect-auto lg:max-h-none lg:flex-1"
      />
      <div class="flex flex-wrap justify-center gap-x-4 gap-y-2 text-sm lg:flex-col lg:justify-center lg:gap-2">
        <div
          v-for="band in [...SPEED_BANDS].reverse()"
          :key="band.label"
          class="flex items-center gap-2"
        >
          <div
            class="size-4 rounded-sm border border-black/10"
            :style="{ backgroundColor: band.color }"
          />
          <span>{{ band.label }}</span>
        </div>
      </div>
    </div>

    <p
      v-if="data"
      class="text-xs text-base-content/50"
    >
      Source: {{ data.source }}.
    </p>
  </div>
</template>
