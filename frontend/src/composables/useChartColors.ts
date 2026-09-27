import { ref, onMounted, onUnmounted } from "vue";

export interface ChartColors {
  axis: string;
  grid: string;
  label: string;
}

function readColors(): ChartColors {
  const style = getComputedStyle(document.documentElement);
  return {
    axis: style.getPropertyValue("--chart-axis").trim() || "#666",
    grid: style.getPropertyValue("--chart-grid").trim() || "rgba(0,0,0,0.1)",
    label: style.getPropertyValue("--chart-label").trim() || "#666",
  };
}

/**
 * Reactive chart colors that update when the theme changes.
 * Listens for both data-theme attribute changes and system color scheme changes.
 */
export function useChartColors() {
  const colors = ref<ChartColors>(readColors());
  let observer: MutationObserver | null = null;
  let mediaQuery: MediaQueryList | null = null;

  const update = () => {
    colors.value = readColors();
  };

  onMounted(() => {
    update();

    observer = new MutationObserver(update);
    observer.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ["data-theme"],
    });

    mediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
    mediaQuery.addEventListener("change", update);
  });

  onUnmounted(() => {
    observer?.disconnect();
    mediaQuery?.removeEventListener("change", update);
  });

  return colors;
}
