import { computed, toValue, type ComputedRef, type MaybeRefOrGetter } from "vue";
import type { TimelineData, TimelineEngine } from "./types";
import { createTimelineEngine } from "./timelineEngine";

export function useTimelineEngine(
  data: MaybeRefOrGetter<TimelineData>,
): ComputedRef<TimelineEngine> {
  return computed(() => createTimelineEngine(toValue(data)));
}
