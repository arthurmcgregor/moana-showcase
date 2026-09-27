import { Chart, registerables, type ChartOptions, type ChartType } from "chart.js";
import annotationPlugin from "chartjs-plugin-annotation";
import zoomPlugin from "chartjs-plugin-zoom";
import "chartjs-adapter-date-fns";
import { computed, type ComputedRef } from "vue";
import { useChartColors, type ChartColors } from "./useChartColors";

let pluginsRegistered = false;
function ensurePluginsRegistered() {
  if (pluginsRegistered) return;
  Chart.register(...registerables, annotationPlugin, zoomPlugin);
  pluginsRegistered = true;
}

export function useChart() {
  ensurePluginsRegistered();
  const colors = useChartColors();

  function buildOptions<T extends ChartType = "line">(
    build: (colors: ChartColors) => ChartOptions<T>,
  ): ComputedRef<ChartOptions<T>> {
    return computed(() => build(colors.value));
  }

  return { buildOptions };
}
