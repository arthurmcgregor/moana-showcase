import { onBeforeUnmount, toValue, type MaybeRefOrGetter } from "vue";
import { useMutation, useQueryCache } from "@pinia/colada";
import {
  updateJourney,
  createAnnotation,
  createMode,
  updateAnnotation,
  updateMode,
  deleteAnnotation,
  deleteMode,
  deleteTrackPoint,
  type AnnotationType,
  type ModeType,
  type JourneyFull,
} from "@/api/web";
import {
  journeyQueryKey,
  journeyModesQueryKey,
  journeyAnnotationsQueryKey,
  journeyTracksQueryKey,
} from "@/composables/useQueries";
import type { TimelineCursorState } from "@/engine";

const TRIM_DEBOUNCE_MS = 300;

export function useJourneyMutations(
  journeyFull: MaybeRefOrGetter<JourneyFull>,
  cursor: TimelineCursorState,
) {
  const queryCache = useQueryCache();

  const journeyId = () => toValue(journeyFull).journey.id;
  const vesselId = () => toValue(journeyFull).journey.vessel;

  const journeyKey = () => journeyQueryKey(journeyId());
  const modesKey = () => journeyModesQueryKey(journeyId());
  const annotationsKey = () => journeyAnnotationsQueryKey(journeyId());

  const { mutate: mutateUpdateJourney } = useMutation({
    mutation: (data: Parameters<typeof updateJourney>[1]) =>
      updateJourney(journeyId(), data),
    onSettled: () => {
      queryCache.invalidateQueries({ key: journeyKey() });
      queryCache.invalidateQueries({ key: ["journeys"] });
    },
  });

  const { mutate: mutateAddAnnotation } = useMutation({
    mutation: (data: { type: AnnotationType; timestamp: string; notes: string }) =>
      createAnnotation({
        vessel: vesselId(),
        type: data.type,
        timestamp: data.timestamp,
        notes: data.notes,
        metadata: {},
      }),
    onSettled: () => queryCache.invalidateQueries({ key: annotationsKey() }),
  });

  const { mutate: mutateAddMode } = useMutation({
    mutation: (data: { mode: ModeType; started_at: string }) =>
      createMode({
        vessel: vesselId(),
        mode: data.mode,
        started_at: data.started_at,
        notes: "",
        metadata: {},
      }),
    onSettled: () => queryCache.invalidateQueries({ key: modesKey() }),
  });

  const { mutate: mutateUpdateAnnotation } = useMutation({
    mutation: ({ id, notes }: { id: number; notes: string }) =>
      updateAnnotation(id, { notes }),
    onSettled: () => queryCache.invalidateQueries({ key: annotationsKey() }),
  });

  // A mode carries only started_at — its end is the next mode's start — so
  // dragging the boundary between two segments moves the later one's start.
  const { mutate: mutateMoveModeStart } = useMutation({
    mutation: ({ id, started_at }: { id: number; started_at: string }) =>
      updateMode(id, { started_at }),
    onSettled: () => queryCache.invalidateQueries({ key: modesKey() }),
  });

  const { mutate: mutateDeleteAnnotation } = useMutation({
    mutation: (id: number) => deleteAnnotation(id),
    onSettled: () => queryCache.invalidateQueries({ key: annotationsKey() }),
  });

  const { mutate: mutateDeleteMode } = useMutation({
    mutation: (id: number) => deleteMode(id),
    onSettled: () => queryCache.invalidateQueries({ key: modesKey() }),
  });

  const { mutate: mutateDeleteTrackPoint } = useMutation({
    mutation: (id: number) => deleteTrackPoint(id),
    onSettled: () => {
      queryCache.invalidateQueries({ key: journeyKey() });
      queryCache.invalidateQueries({ key: modesKey() });
      queryCache.invalidateQueries({ key: annotationsKey() });
      queryCache.invalidateQueries({ key: journeyTracksQueryKey(journeyId()) });
      cursor.resetCursor();
    },
  });

  let trimDebounceTimer: ReturnType<typeof setTimeout> | null = null;

  function debouncedTrim(data: Parameters<typeof mutateUpdateJourney>[0]) {
    if (trimDebounceTimer !== null) clearTimeout(trimDebounceTimer);
    trimDebounceTimer = setTimeout(() => {
      mutateUpdateJourney(data);
      trimDebounceTimer = null;
    }, TRIM_DEBOUNCE_MS);
  }

  onBeforeUnmount(() => {
    if (trimDebounceTimer !== null) clearTimeout(trimDebounceTimer);
  });

  function trimStart() {
    if (!cursor.cursorTime.value) return;
    debouncedTrim({ started_at: cursor.cursorTime.value });
  }

  function trimEnd() {
    if (!cursor.cursorTime.value) return;
    debouncedTrim({ ended_at: cursor.cursorTime.value });
  }

  function addModeAtCursor(mode: string) {
    if (!cursor.cursorTime.value) return;
    mutateAddMode({ mode: mode as ModeType, started_at: cursor.cursorTime.value });
  }

  function splitModeAtCursor() {
    const active = cursor.activeMode.value;
    if (!active || !cursor.cursorTime.value) return;
    mutateAddMode({
      mode: active.mode as ModeType,
      started_at: cursor.cursorTime.value,
    });
  }

  function addAnnotation(type: AnnotationType, notes: string) {
    if (!cursor.cursorTime.value) return;
    mutateAddAnnotation({ type, timestamp: cursor.cursorTime.value, notes });
  }

  function updateAnnotationNotes(payload: { id: number; notes: string }) {
    mutateUpdateAnnotation(payload);
  }

  function deleteSnappedPoint() {
    const id = cursor.snappedPoint.value?.feature.properties.id;
    if (id == null) return;
    mutateDeleteTrackPoint(id);
  }

  return {
    trimStart,
    trimEnd,
    addModeAtCursor,
    splitModeAtCursor,
    deleteMode: mutateDeleteMode,
    addAnnotation,
    moveModeStart: mutateMoveModeStart,
    updateAnnotationNotes,
    deleteAnnotation: mutateDeleteAnnotation,
    deleteSnappedPoint,
  };
}
