<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from "vue";
import { useChartColors } from "@/composables/useChartColors";

export interface WindBin {
  label: string;
  angle: number; // meteorological degrees: 0=N, clockwise
  bands: number[]; // count per speed band, innermost first
  total: number;
}

export interface SpeedBand {
  label: string;
  color: string;
}

const props = defineProps<{
  bins: WindBin[];
  speedBands: SpeedBand[];
  totalObs: number;
}>();

const canvasRef = ref<HTMLCanvasElement | null>(null);
const containerRef = ref<HTMLDivElement | null>(null);
const chartColors = useChartColors();

function draw() {
  const canvas = canvasRef.value;
  const container = containerRef.value;
  if (!canvas || !container || !props.bins.length || props.totalObs === 0) return;

  const size = Math.min(container.clientWidth, container.clientHeight);
  if (size <= 0) return;

  const dpr = window.devicePixelRatio || 1;
  canvas.width = size * dpr;
  canvas.height = size * dpr;
  canvas.style.width = `${size}px`;
  canvas.style.height = `${size}px`;

  const ctx = canvas.getContext("2d")!;
  ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, size, size);

  const colors = chartColors.value;
  const baseBg = getComputedStyle(document.documentElement)
    .getPropertyValue("--color-base-200").trim();

  const cx = size / 2;
  const cy = size / 2;
  const labelPad = 36;
  const innerR = 14;
  const maxR = size / 2 - labelPad;

  const maxBinTotal = Math.max(...props.bins.map((b) => b.total), 1);
  const maxPct = (maxBinTotal / props.totalObs) * 100;

  // Frequency rings + NNE-offset percentage labels (positioned off the East
  // ray so they don't collide with the cardinal compass labels).
  const ringStep = maxPct > 20 ? 10 : maxPct > 10 ? 5 : maxPct > 4 ? 2 : 1;
  ctx.setLineDash([3, 3]);
  ctx.strokeStyle = colors.grid;
  ctx.fillStyle = colors.axis;
  ctx.font = "8px ui-monospace, SFMono-Regular, Menlo, monospace";
  const labelAngle = ((22.5 - 90) * Math.PI) / 180;
  const labelCos = Math.cos(labelAngle);
  const labelSin = Math.sin(labelAngle);
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  for (let pct = ringStep; pct <= maxPct + 0.01; pct += ringStep) {
    const r = innerR + ((pct / maxPct) * (maxR - innerR));
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.stroke();
    const lx = cx + r * labelCos;
    const ly = cy + r * labelSin;
    const textWidth = ctx.measureText(`${pct.toFixed(0)}%`).width;
    ctx.save();
    ctx.setLineDash([]);
    ctx.fillStyle = baseBg || "rgba(0,0,0,0)";
    ctx.fillRect(lx - textWidth / 2 - 2, ly - 6, textWidth + 4, 12);
    ctx.restore();
    ctx.fillStyle = colors.axis;
    ctx.fillText(`${pct.toFixed(0)}%`, lx, ly);
  }
  ctx.setLineDash([]);

  // Wind sector petals
  const halfSector = (11.25 * Math.PI) / 180;
  for (const bin of props.bins) {
    if (bin.total === 0) continue;
    // Convert met direction to canvas angle (0=East, CW)
    const center = ((bin.angle - 90) * Math.PI) / 180;
    const a0 = center - halfSector;
    const a1 = center + halfSector;

    let cumCount = 0;
    for (let i = 0; i < props.speedBands.length; i++) {
      const count = bin.bands[i] ?? 0;
      if (count === 0) {
        cumCount += count;
        continue;
      }
      const r1 = innerR + (cumCount / maxBinTotal) * (maxR - innerR);
      cumCount += count;
      const r2 = innerR + (cumCount / maxBinTotal) * (maxR - innerR);

      ctx.beginPath();
      ctx.arc(cx, cy, r2, a0, a1);
      ctx.arc(cx, cy, r1, a1, a0, true);
      ctx.closePath();
      ctx.fillStyle = props.speedBands[i].color;
      ctx.fill();
      ctx.strokeStyle = "rgba(255,255,255,0.4)";
      ctx.lineWidth = 0.5;
      ctx.stroke();
    }
  }

  // Compass labels
  const allDirs = [
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
  ];
  ctx.textBaseline = "middle";
  ctx.textAlign = "center";
  for (let i = 0; i < 16; i++) {
    const angle = ((i * 22.5 - 90) * Math.PI) / 180;
    const r = maxR + 20;
    const x = cx + r * Math.cos(angle);
    const y = cy + r * Math.sin(angle);
    const isCardinal = i % 4 === 0;
    ctx.font = `${isCardinal ? "bold" : "normal"} ${isCardinal ? 12 : 10}px sans-serif`;
    ctx.fillStyle = isCardinal ? colors.label : colors.axis;
    ctx.fillText(allDirs[i], x, y);
  }

  // Inner circle
  ctx.beginPath();
  ctx.arc(cx, cy, innerR, 0, Math.PI * 2);
  ctx.fillStyle = baseBg || "#f0f0f0";
  ctx.fill();
  ctx.strokeStyle = colors.grid;
  ctx.lineWidth = 1;
  ctx.stroke();
}

const ro = new ResizeObserver(() => draw());

onMounted(() => {
  if (containerRef.value) ro.observe(containerRef.value);
  draw();
});

onUnmounted(() => ro.disconnect());

watch(() => [props.bins, props.totalObs, chartColors.value], draw, { deep: true });
</script>

<template>
  <div
    ref="containerRef"
    class="flex h-full w-full items-center justify-center"
  >
    <canvas ref="canvasRef" />
  </div>
</template>
