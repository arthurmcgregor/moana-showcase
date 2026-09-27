<script setup lang="ts">
import { computed } from "vue";
import type { WeatherSummarySeriesPoint } from "@/api/web";
import {
  hasSeries,
  seriesTrend,
  seriesValueAt,
  type LiveSample,
  type LiveWeatherSample,
  type SeriesMetric,
  type WeatherWindowStats,
} from "@/engine";
import { formatClockTime } from "@/utils/formatters";
import StatIcon from "./StatIcon.vue";
import SpeedGauge from "./SpeedGauge.vue";
import WindGauge from "./WindGauge.vue";
import type { StatIconName } from "./statIcons";

// Everything true at the playhead, over the water it was true on: what the boat
// was doing, then the state of the sea and the air, then the wind as a dial.
//
// The boat's own figures were under the deck until they were read against what
// the rail already said. A selected segment's row carries its distance and its
// duration, a leg's card carries the leg's, and the journey's sit at the head of
// the rail — so the panel under the deck was saying all three a second time. Its
// speed and its mode were not repeated anywhere, and they are readings at a
// moment rather than totals over a stretch, so they belong here with the rest of
// the instruments and the panel itself is gone.
//
// The tide and the temperature used to be drawn as the day's curve. Neither
// earned it: the tide's shape is the most predictable line on the screen — it
// goes up, it comes down, twice — and the temperature's is a slow drift. What
// you actually want off them is the reading now and which way it is going,
// which is a number and an arrow each.
//
// An arrow and no word: the direction is the whole message, and two lines of
// prose over the water read as clutter. The words survive as the tooltip and the
// screen-reader text, which is where a reader who wants them looks. Slack water
// is the exception that has to stay legible without them — an arrow that has
// stopped is not a mark — so it gets its own glyph and the accent colour,
// because the turn of the tide is the one moment on the row worth catching.
//
// The wind dial sits here rather than in the deck's readout because it is the
// one figure with a direction as well as a size, which makes it a picture, and
// a picture wants width the deck's text column could not spare. Beside the map
// it also reads as what it is — the weather where the boat was.

const props = defineProps<{
  series: WeatherSummarySeriesPoint[];
  /** The moment being read: the playhead, or the end of the scope. */
  markerMs: number | null;
  /** The wind over the scoped stretch, which the dial's bars are read against. */
  weather: WeatherWindowStats;
  /** The station's readings at the marked moment. */
  liveWeather: LiveWeatherSample;
  dayGustMax: number | null;
  /** The boat's own instruments at the marked moment. */
  liveStats: LiveSample;
  /** The mode the playhead is passing through, named and coloured as the deck. */
  mode: { label: string; color: string } | null;
  /** Whether this boat records heel at all — a dash forever if it does not. */
  hasHeel?: boolean;
  /** The fastest the boat went over the scope — the top of the speed dial. */
  scopeSpeedMax?: number | null;
  /** The fastest it had gone by the marked moment — the dial's peak line. */
  peakSpeed?: number | null;
}>();

interface Row {
  metric: SeriesMetric;
  label: string;
  icon: StatIconName;
  unit: string;
  digits: number;
  /** Below this many units an hour it counts as holding still. */
  flatPerHour: number;
  /** What the trend is called — tooltip and screen-reader text only. */
  words: { rising: string; falling: string; steady: string };
  /** Whether holding still is itself an event worth drawing attention to. */
  markSteady?: boolean;
  /**
   * Where the row's figure comes from, for a row that must not read its own
   * series. The wind is the case: the dial below it is fed from the live sample,
   * which stops rather than holding the last reading across a station outage,
   * and a row disagreeing with the picture under it would be worse than either
   * answer alone.
   */
  live?: (sample: LiveWeatherSample) => number | null;
  /** Opens a new block: a rule above it, and the dial beneath. */
  startsGroup?: boolean;
}

const ROWS: Row[] = [
  {
    metric: "tide",
    label: "Tide",
    icon: "tide",
    unit: " m",
    digits: 2,
    flatPerHour: 0.05,
    words: { rising: "flooding", falling: "ebbing", steady: "slack water" },
    markSteady: true,
  },
  {
    metric: "temperature",
    label: "Temp",
    icon: "temperature",
    unit: "°C",
    digits: 1,
    flatPerHour: 0.3,
    words: { rising: "warming", falling: "cooling", steady: "steady" },
  },
  {
    metric: "wind_speed",
    label: "Wind",
    icon: "wind",
    unit: " kts",
    digits: 1,
    // Wind is the noisiest thing the station measures; under a knot and a half
    // an hour the fit is reading its own scatter, not a change in the weather.
    flatPerHour: 1.5,
    words: { rising: "freshening", falling: "easing", steady: "steady" },
    live: (sample) => sample.windSpeed,
    startsGroup: true,
  },
];

const readings = computed(() =>
  ROWS.filter((row) => hasSeries(props.series, row.metric)).map((row) => {
    const at = props.markerMs;
    const value = row.live
      ? row.live(props.liveWeather)
      : at === null ? null : seriesValueAt(props.series, row.metric, at);
    const trend = at === null ? null : seriesTrend(props.series, row.metric, at, row.flatPerHour);
    return {
      ...row,
      text: value === null ? "—" : `${value.toFixed(row.digits)}${row.unit}`,
      trend,
      trendWord: trend === null ? null : row.words[trend],
    };
  }),
);

/**
 * The moment the whole panel is answering for. It sat with the transport under
 * the deck, which is where the controls are but not where the readings are — a
 * time is a reading, and it belongs at the head of the figures it stamps.
 */
const clock = computed(() =>
  props.markerMs === null ? "--:--" : formatClockTime(new Date(props.markerMs).toISOString()),
);

/**
 * What the boat was doing that is a figure rather than a picture. The speed is
 * the dial below these and the mode is its caption; heel is what is left, and
 * only where the boat records it.
 * No trend arrows — these are instruments, not a tide, and a rate of change is
 * not what you read off them. They keep the arrow's width all the same, so every
 * figure in the panel lines up in one column.
 */
const boatRows = computed(() => {
  const rows: { key: string; label: string; icon: StatIconName; text: string }[] = [];

  const { heel } = props.liveStats;
  if (props.hasHeel) {
    rows.push({
      key: "heel",
      label: "Heel",
      icon: "heel",
      text: heel === null ? "—" : `${heel.toFixed(0)}°`,
    });
  }

  return rows;
});

/** No speed anywhere in the scope means no dial, rather than an empty one. */
const hasSpeed = computed(() => props.scopeSpeedMax != null);

// A station that never reported wind gets no dial rather than an empty one —
// the same test the rows above make for their own metric.
const hasWind = computed(
  () =>
    props.weather.wind !== null
    || props.weather.gustMax !== null
    || props.liveWeather.windSpeed !== null
    || props.liveWeather.gust !== null,
);
</script>

<template>
  <!-- Click-through: the map underneath is the thing being used, and this is a
       label on it rather than a control. Its width is the dial's — the rows
       above would take less, but a panel that changes width with the weather
       reads as a glitch. -->
  <div
    v-if="boatRows.length || hasSpeed || readings.length || hasWind"
    class="pointer-events-none w-[184px] rounded-lg border border-base-300/60 bg-base-100/80 px-2.5 py-2 shadow-lg backdrop-blur"
  >
    <div
      v-if="boatRows.length || hasSpeed"
      class="mb-2 border-b border-base-300/60 pb-2"
    >
      <div class="flex flex-col gap-1.5">
        <div
          v-for="row in boatRows"
          :key="row.key"
          class="flex items-center gap-2"
        >
          <StatIcon
            :name="row.icon"
            class="size-3.5 shrink-0 text-base-content/40"
          />
          <span class="font-mono text-[10px] uppercase tracking-wider text-base-content/40">
            {{ row.label }}
          </span>
          <span class="ml-auto truncate text-sm font-medium tabular-nums">{{ row.text }}</span>
          <!-- Holds the column the trend arrows opposite occupy. -->
          <span class="w-4 shrink-0" />
        </div>
      </div>

      <SpeedGauge
        v-if="hasSpeed"
        class="mt-1"
        :speed="liveStats.speed"
        :scope-max="scopeSpeedMax ?? null"
        :peak="peakSpeed ?? null"
      />

      <!-- The mode captions the dial rather than taking a labelled row of its
           own: "Motoring" under a speed is a sentence, and the word "Mode" in
           front of it was carrying nothing the colour and the word did not. -->
      <div
        v-if="mode"
        class="mt-0.5 flex items-center justify-center gap-1.5"
      >
        <span
          class="size-2 shrink-0 rounded-full"
          :style="{ backgroundColor: mode.color }"
        />
        <span class="truncate text-sm font-medium">{{ mode.label }}</span>
      </div>
    </div>

    <!-- One loop for all three, so the wind's figure lands in the same column as
         the tide's and the temperature's and carries the same trend arrow. The
         rule that separates the water from the air rides on the wind row rather
         than sitting between two loops. -->
    <div class="flex flex-col gap-1.5">
      <!-- The moment every figure in the panel is read at, at the head of the
           readings rather than in a band of its own. -->
      <div class="flex items-center gap-2">
        <StatIcon
          name="duration"
          class="size-3.5 shrink-0 text-base-content/40"
        />
        <span class="font-mono text-[10px] uppercase tracking-wider text-base-content/40">
          Time
        </span>
        <span class="ml-auto text-sm font-medium tabular-nums">{{ clock }}</span>
        <span class="w-4 shrink-0" />
      </div>

      <div
        v-for="row in readings"
        :key="row.metric"
        class="flex items-center gap-2"
        :class="row.startsGroup ? 'mt-1 border-t border-base-300/60 pt-2' : ''"
      >
        <StatIcon
          :name="row.icon"
          class="size-3.5 shrink-0 text-base-content/40"
        />
        <span class="font-mono text-[10px] uppercase tracking-wider text-base-content/40">
          {{ row.label }}
        </span>
        <span class="ml-auto text-sm font-medium tabular-nums">{{ row.text }}</span>

        <!-- The arrow carries it; the word is the tooltip. Slack keeps a glyph of
             its own, since an absent arrow and a missing reading would otherwise
             look the same. -->
        <span
          v-if="row.trendWord"
          class="flex w-4 shrink-0 items-center justify-center"
          :class="row.trend === 'steady' && row.markSteady
            ? 'text-accent'
            : 'text-base-content/45'"
          :title="`${row.label} ${row.trendWord}`"
        >
          <OutlineArrowUpIcon
            v-if="row.trend === 'rising'"
            class="size-3.5 shrink-0"
          />
          <OutlineArrowDownIcon
            v-else-if="row.trend === 'falling'"
            class="size-3.5 shrink-0"
          />
          <OutlineArrowsRightLeftIcon
            v-else
            class="size-3.5 shrink-0"
          />
          <span class="sr-only">{{ row.trendWord }}</span>
        </span>
      </div>
    </div>

    <WindGauge
      v-if="hasWind"
      class="mt-1"
      :weather="weather"
      :day-gust-max="dayGustMax"
      :live-wind="liveWeather.windSpeed"
      :live-gust="liveWeather.gust"
      :live-direction-deg="liveWeather.directionDeg"
    />
  </div>
</template>
