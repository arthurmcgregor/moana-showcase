<script setup lang="ts">
import { Line } from "vue-chartjs";
import type { TooltipItem } from "chart.js";
import type { AnnotationOptions } from "chartjs-plugin-annotation";
import { computed } from "vue";
import type { GeoJSONFeatureCollection, JourneyFull, WeatherSummarySeriesPoint } from "@/api/web";
import type { TravelMode } from "@/api/types";
import { getAnnotationColor, getModeColor, modeTypes } from "@/constants/journeyTypes";
import { useTimelineEngine } from "@/engine";
import type { TimelineData } from "@/engine";
import { useChart } from "@/composables/useChart";

const props = defineProps<{
  journeyFull?: JourneyFull;
  journeyTracks: GeoJSONFeatureCollection;
  travelMode?: TravelMode;
  windSeries?: WeatherSummarySeriesPoint[];
}>();

// Convert knots to km/h
const knotsToKmh = (knots: number) => knots * 1.852;

// Timeline engine for mode color lookups
const timelineData = computed<TimelineData>(() => ({
  features: props.journeyTracks,
  modes: props.journeyFull?.modes ?? [],
  annotations: props.journeyFull?.annotations ?? [],
  journeyStart: props.journeyFull?.journey.started_at ?? new Date().toISOString(),
  journeyEnd: props.journeyFull?.journey.ended_at ?? null,
}));

const engine = useTimelineEngine(timelineData);

// Batch compute mode colors for all features
const featureModeColors = computed(() =>
  engine.value.modeColorsForFeatures(props.journeyTracks.features),
);

const defaultColor = "rgba(30, 64, 175, 0.6)";

const activeModes = computed(() => {
  const modes = props.journeyFull?.modes ?? [];
  if (!modes.length) return [];
  const seen = new Set<string>();
  return modes
    .filter((m) => {
      if (seen.has(m.mode)) return false;
      seen.add(m.mode);
      return true;
    })
    .map((m) => ({
      label: modeTypes[m.mode]?.icon ?? m.mode,
      color: getModeColor(m.mode),
    }));
});

const speedUnit = computed(() => (props.travelMode === "sailing" ? "knots" : "km/h"));

// Compute chart points, colors, and mode labels in a single pass
const chartComputed = computed(() => {
  const features = props.journeyTracks.features;
  if (!features || features.length === 0) {
    return { points: [], pointColors: [], pointModes: [] };
  }

  const colors = featureModeColors.value;
  const modes = [...(props.journeyFull?.modes ?? [])].sort(
    (a, b) => new Date(a.started_at).getTime() - new Date(b.started_at).getTime(),
  );

  const points: { x: number; y: number }[] = [];
  const pointColors: string[] = [];
  const pointModes: string[] = [];

  // Sweep pointer for mode labels (mirrors modeColorsForFeatures approach)
  let modeIdx = 0;
  let currentLabel = "";

  for (let i = 0; i < features.length; i++) {
    const timestamp = new Date(features[i].properties?.timestamp);
    if (!timestamp || isNaN(timestamp.getTime())) continue;

    const ts = timestamp.getTime();

    // Advance mode pointer
    while (modeIdx < modes.length - 1) {
      const nextStart = new Date(modes[modeIdx + 1].started_at).getTime();
      if (nextStart <= ts) {
        modeIdx++;
        currentLabel = modeTypes[modes[modeIdx].mode]?.label ?? modes[modeIdx].mode;
      } else break;
    }
    if (modes.length > 0 && modeIdx === 0 && !currentLabel) {
      const firstStart = new Date(modes[0].started_at).getTime();
      if (firstStart <= ts) {
        currentLabel = modeTypes[modes[0].mode]?.label ?? modes[0].mode;
      }
    }

    const speed = features[i].properties?.speed || 0;
    points.push({
      x: ts,
      y: props.travelMode === "sailing" ? speed : knotsToKmh(speed),
    });
    pointColors.push(colors[i] ?? defaultColor);
    pointModes.push(currentLabel);
  }

  return { points, pointColors, pointModes };
});

function attitudeSeries(getter: (props: Record<string, unknown>) => unknown) {
  const features = props.journeyTracks.features;
  const out: { x: number; y: number }[] = [];
  for (const f of features) {
    const props_ = (f.properties ?? {}) as Record<string, unknown>;
    const v = getter(props_);
    if (typeof v !== "number" || !Number.isFinite(v)) continue;
    const ts = new Date(props_.timestamp as string).getTime();
    if (Number.isNaN(ts)) continue;
    out.push({ x: ts, y: v });
  }
  return out;
}

const heelLinePoints = computed(() => attitudeSeries((p) => p.heel));
const trimLinePoints = computed(() => attitudeSeries((p) => p.trim));

const windLinePoints = computed(() => {
  const series = props.windSeries;
  if (!series || series.length === 0) return [];
  const toDisplay = (s: number) => (props.travelMode === "sailing" ? s : knotsToKmh(s));
  const out: { x: number; y: number }[] = [];
  for (const p of series) {
    if (p.wind_speed == null) continue;
    const ts = new Date(p.t).getTime();
    if (Number.isNaN(ts)) continue;
    out.push({ x: ts, y: toDisplay(p.wind_speed) });
  }
  return out;
});

const chartData = computed(() => {
  const { points, pointColors } = chartComputed.value;
  const windDataset = windLinePoints.value.length > 0 ? [{
    label: `Wind speed (${speedUnit.value})`,
    data: windLinePoints.value,
    borderColor: "#6366f1",
    backgroundColor: "#6366f1",
    borderWidth: 1.5,
    pointRadius: 0,
    pointHoverRadius: 3,
    tension: 0.2,
    spanGaps: false,
    showLine: true,
  }] : [];

  const heelDataset = heelLinePoints.value.length > 0 ? [{
    label: "Heel (°, + stbd)",
    data: heelLinePoints.value,
    borderColor: "#f59e0b",
    backgroundColor: "#f59e0b",
    borderWidth: 1.5,
    pointRadius: 0,
    pointHoverRadius: 3,
    tension: 0.2,
    spanGaps: false,
    showLine: true,
    yAxisID: "y1",
  }] : [];

  const trimDataset = trimLinePoints.value.length > 0 ? [{
    label: "Trim (°, + bow up)",
    data: trimLinePoints.value,
    borderColor: "#ec4899",
    backgroundColor: "#ec4899",
    borderWidth: 1.5,
    pointRadius: 0,
    pointHoverRadius: 3,
    tension: 0.2,
    spanGaps: false,
    showLine: true,
    yAxisID: "y1",
  }] : [];

  if (points.length === 0) {
    return {
      labels: [],
      datasets: [
        {
          label: `Speed (${speedUnit.value})`,
          data: [],
          backgroundColor: "rgba(30, 64, 175, 0.85)",
          borderColor: "rgba(30, 64, 175, 1)",
          borderWidth: 1,
        },
        ...windDataset,
        ...heelDataset,
        ...trimDataset,
      ],
    };
  }

  return {
    datasets: [
      {
        label: `Speed (${speedUnit.value})`,
        data: points,
        backgroundColor: pointColors,
        borderColor: pointColors,
        borderWidth: 0,
        pointRadius: 2,
        pointHoverRadius: 4,
        pointBackgroundColor: pointColors,
        pointBorderColor: pointColors,
        pointBorderWidth: 1,
        showLine: false,
      },
      ...windDataset,
      ...heelDataset,
      ...trimDataset,
    ],
  };
});

const { buildOptions } = useChart();

const chartOptions = buildOptions((colors) => ({
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      display:
        windLinePoints.value.length > 0 ||
        heelLinePoints.value.length > 0 ||
        trimLinePoints.value.length > 0,
      labels: {
        color: colors.label,
        // Show only the overlay datasets; the boat-speed dataset has no legend entry.
        filter: (item) =>
          item.text?.startsWith("Wind speed") ||
          item.text?.startsWith("Heel") ||
          item.text?.startsWith("Trim") ||
          false,
        boxWidth: 12,
        font: { size: 11 },
      },
    },
    title: {
      display: true,
      text: `Speed over Time (${speedUnit.value})`,
      color: colors.label,
    },
    tooltip: {
      mode: "nearest",
      intersect: true,
      displayColors: false,
      filter: (_item, index) => index === 0,
      callbacks: {
        title: (context: TooltipItem<"line">[]) => {
          const val = context[0].parsed.y;
          if (val === null) return "";
          return `${val.toFixed(2)} ${speedUnit.value}`;
        },
        label: (context: TooltipItem<"line">) => {
          const mode = chartComputed.value.pointModes[context.dataIndex];
          return mode || "";
        },
        afterBody: (context: TooltipItem<"line">[]) => {
          const annotations = props.journeyFull?.annotations;
          if (!annotations?.length) return [];

          const hoveredTime = context[0].parsed.x;
          if (hoveredTime === null) return [];

          const nearby = annotations.filter((ann) => {
            const annTime = new Date(ann.timestamp).getTime();
            return Math.abs(annTime - hoveredTime) < 5 * 60 * 1000;
          });

          return nearby.map((ann) => {
            const isPending = ann.id < 0 ? " (Pending)" : "";
            const notes = ann.notes ? ` - ${ann.notes}` : "";
            return `${ann.type}${isPending}${notes}`;
          });
        },
      },
      titleFont: { size: 14, weight: "bold" },
    },
    annotation: {
      annotations: (props.journeyFull?.annotations ?? []).reduce(
        (acc, annotation, index) => {
          const timestamp = new Date(annotation.timestamp).getTime();
          const color = getAnnotationColor(annotation.type);
          const isPending = annotation.id < 0;

          acc[`annotation${index}`] = {
            type: "line",
            xMin: timestamp,
            xMax: timestamp,
            borderColor: color,
            borderWidth: 2,
            borderDash: isPending ? [5, 5] : [],
            label: {
              display: true,
              content: annotation.type,
              position: "start",
              backgroundColor: color,
              color: "white",
              font: { size: 10, weight: "bold" },
              padding: 4,
              yAdjust: -10,
            },
          };
          return acc;
        },
        {} as Record<string, AnnotationOptions>,
      ),
    },
    zoom: {
      pan: { enabled: true, mode: "x" },
      zoom: {
        wheel: { enabled: true },
        pinch: { enabled: true },
        mode: "x",
      },
    },
  },
  scales: {
    x: {
      type: "time",
      time: {
        displayFormats: {
          hour: "HH:mm",
          minute: "HH:mm",
          second: "HH:mm:ss",
        },
      },
      title: { display: true, text: "Time", color: colors.label },
      ticks: { autoSkip: true, maxTicksLimit: 15, color: colors.axis },
      grid: { color: colors.grid },
    },
    y: {
      beginAtZero: true,
      title: { display: true, text: speedUnit.value, color: colors.label },
      ticks: { color: colors.axis },
      grid: { color: colors.grid },
    },
    ...(heelLinePoints.value.length > 0 || trimLinePoints.value.length > 0 ? {
      y1: {
        position: "right" as const,
        title: { display: true, text: "Attitude (°)", color: colors.label },
        ticks: {
          color: colors.axis,
          callback: (value: number | string) => {
            const v = typeof value === "number" ? value : Number(value);
            if (!Number.isFinite(v)) return value;
            return `${v}°`;
          },
        },
        grid: { drawOnChartArea: false },
      },
    } : {}),
  },
}));
</script>

<template>
  <div
    v-if="journeyTracks.features.length > 0"
    class="flex h-full w-full flex-col"
  >
    <div class="min-h-0 flex-1 max-md:h-64 max-md:flex-none">
      <Line
        :data="chartData"
        :options="chartOptions"
      />
    </div>
    <div
      v-if="activeModes.length"
      class="flex flex-wrap items-center gap-x-4 gap-y-1 pt-3"
    >
      <div
        v-for="mode in activeModes"
        :key="mode.label"
        class="flex items-center gap-1.5"
      >
        <span
          class="inline-block size-2 rounded-full"
          :style="{ backgroundColor: mode.color }"
        />
        <span class="font-mono text-[11px] uppercase tracking-wider text-base-content/50">
          {{ mode.label }}
        </span>
      </div>
    </div>
  </div>
</template>
