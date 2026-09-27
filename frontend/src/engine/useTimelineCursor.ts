import { ref, computed, type Ref, type ComputedRef, toValue, type MaybeRefOrGetter } from "vue";
import type { Mode, Annotation } from "@/api/web";
import type { TimelineData, SnappedPoint } from "./types";
import { createTimelineEngine } from "./timelineEngine";
import { formatTimestampMs, formatDurationMs } from "@/utils/formatters";

export interface TimelineCursorState {
  /** Canonical cursor state — an ISO timestamp string, null when no cursor active */
  cursorTime: Ref<string | null>;

  // Derived (read-only)
  percentage: ComputedRef<number>;
  snappedPoint: ComputedRef<SnappedPoint | null>;
  cursorPosition: ComputedRef<[number, number] | null>;
  activeMode: ComputedRef<Mode | null>;
  precedingMode: ComputedRef<Mode | null>;
  cursorAtModeStart: ComputedRef<boolean>;
  nearbyAnnotations: ComputedRef<Annotation[]>;
  modeGradientCSS: ComputedRef<string>;
  formattedTime: ComputedRef<string>;
  elapsed: ComputedRef<string>;

  // Actions
  setCursorFromPercentage(pct: number): void;
  setCursorFromCoordinate(coord: [number, number]): void;
  setCursorTime(iso: string): void;
  /** Move `count` fixes along the track; shift-stepping jumps ten at a time. */
  stepPoint(direction: 1 | -1, count?: number): void;
  /** Nearest fix time to an arbitrary moment — what dragging snaps to. */
  snapMs(epochMs: number): number | null;
  snapMsAtOrAfter(epochMs: number): number | null;
  stepModeBoundary(direction: 1 | -1): void;
  resetCursor(): void;
  clearCursor(): void;
}

export function useTimelineCursor(data: MaybeRefOrGetter<TimelineData>): TimelineCursorState {
  const engine = computed(() => createTimelineEngine(toValue(data)));

  const cursorTime = ref<string | null>(null);

  const lastPointIndex = computed(() => toValue(data).features.features.length - 1);

  const cursorMs = computed(() =>
    cursorTime.value ? new Date(cursorTime.value).getTime() : null,
  );

  const percentage = computed(() => {
    if (cursorMs.value === null) return 0;
    return engine.value.timeToPercentage(cursorMs.value);
  });

  const snappedPoint = computed(() => {
    if (cursorMs.value === null) return null;
    return engine.value.snapToPoint(cursorMs.value);
  });

  const cursorPosition = computed<[number, number] | null>(() => {
    const point = snappedPoint.value;
    if (!point) return null;
    return point.feature.geometry.coordinates;
  });

  const activeMode = computed(() => {
    if (cursorMs.value === null) return null;
    return engine.value.modeAtTime(cursorMs.value);
  });

  const precedingMode = computed(() => {
    if (cursorMs.value === null) return null;
    return engine.value.modeBefore(cursorMs.value);
  });

  const cursorAtModeStart = computed(() => {
    if (cursorMs.value === null) return false;
    return engine.value.isNearModeStart(cursorMs.value);
  });

  const nearbyAnnotations = computed(() => {
    if (cursorMs.value === null) return [];
    return engine.value.annotationsNear(cursorMs.value);
  });

  const modeGradientCSS = computed(() => engine.value.modeGradientCSS());

  const formattedTime = computed(() => {
    if (cursorMs.value === null) return "";
    return formatTimestampMs(cursorMs.value);
  });

  const elapsed = computed(() => {
    if (cursorMs.value === null) return "";
    return formatDurationMs(cursorMs.value - engine.value.startMs);
  });

  function snapMs(epochMs: number): number | null {
    const snapped = engine.value.snapToPoint(epochMs);
    if (!snapped) return null;
    return new Date(snapped.feature.properties.timestamp).getTime();
  }

  /**
   * Snap forwards rather than to whichever sample is closest. Mode boundaries
   * carry sub-second precision and GPS samples land on whole seconds, so the
   * nearest sample to the start of a selection is usually the one just before
   * it — which puts the playhead in the segment you did not choose.
   *
   * Past the last sample there is nothing to move forward onto, so it falls
   * back to the nearest.
   */
  function snapMsAtOrAfter(epochMs: number): number | null {
    const snapped = engine.value.snapToPointAtOrAfter(epochMs);
    if (!snapped) return snapMs(epochMs);
    return new Date(snapped.feature.properties.timestamp).getTime();
  }

  function setCursorFromPercentage(pct: number) {
    const ts = engine.value.percentageToTime(pct);
    cursorTime.value = new Date(ts).toISOString();
  }

  function setCursorTime(iso: string) {
    cursorTime.value = iso;
  }

  function setCursorFromCoordinate(coord: [number, number]) {
    const point = engine.value.nearestPointToCoordinate(coord);
    if (point) {
      cursorTime.value = point.feature.properties.timestamp;
    }
  }

  function stepPoint(direction: 1 | -1, count = 1) {
    if (cursorMs.value === null) {
      resetCursor();
      return;
    }
    const snapped = engine.value.snapToPoint(cursorMs.value);
    if (!snapped) return;
    // A jump that would run off the end still moves — it lands on the last fix
    // rather than doing nothing.
    const target = snapped.index + direction * Math.max(1, count);
    const next = engine.value.pointAtIndex(target) ?? engine.value.pointAtIndex(
      direction > 0 ? lastPointIndex.value : 0,
    );
    if (next && next.index !== snapped.index) {
      cursorTime.value = next.feature.properties.timestamp;
    }
  }

  function stepModeBoundary(direction: 1 | -1) {
    if (cursorMs.value === null) {
      resetCursor();
      return;
    }
    const ts =
      direction > 0
        ? engine.value.modeBoundaryAfter(cursorMs.value)
        : engine.value.modeBoundaryBefore(cursorMs.value);
    if (ts !== null) cursorTime.value = new Date(ts).toISOString();
  }

  function resetCursor() {
    cursorTime.value = new Date(engine.value.startMs).toISOString();
  }

  function clearCursor() {
    cursorTime.value = null;
  }

  return {
    cursorTime,
    percentage,
    snappedPoint,
    cursorPosition,
    activeMode,
    precedingMode,
    cursorAtModeStart,
    nearbyAnnotations,
    modeGradientCSS,
    formattedTime,
    elapsed,
    setCursorFromPercentage,
    setCursorFromCoordinate,
    setCursorTime,
    snapMs,
    snapMsAtOrAfter,
    stepPoint,
    stepModeBoundary,
    resetCursor,
    clearCursor,
  };
}
