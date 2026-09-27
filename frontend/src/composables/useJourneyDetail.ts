import { ref, computed, toValue, type MaybeRefOrGetter, type ComputedRef, type Ref, type WritableComputedRef } from "vue";
import { useMutation, useQueryCache } from "@pinia/colada";
import {
  deleteJourney,
  updateJourney,
  type Journey,
  type JourneyFull,
  type Mode,
  type Annotation,
  type WeatherStation,
  type WeatherSummary,
  type GeoJSONFeatureCollection,
} from "@/api/web";
import type { TimelineData, TimelineCursorState  } from "@/engine";
import {
  journeyQueryKey,
  journeyTracksQueryKey,
  journeyWeatherSummaryQueryKey,
  useJourneyQuery,
  useJourneyModesQuery,
  useJourneyAnnotationsQuery,
  useJourneyAboardQuery,
  useJourneyTracksQuery,
  useJourneyWeatherSummaryQuery,
  useWeatherStationsQuery,
} from "@/composables/useQueries";

const JOURNEY_LIST_FIELDS: (keyof Journey)[] = ["name", "description", "started_at", "ended_at", "vessel"];

const EMPTY_FEATURE_COLLECTION: GeoJSONFeatureCollection = Object.freeze({
  type: "FeatureCollection" as const,
  features: [] as GeoJSONFeatureCollection["features"],
});

export interface UseJourneyDetailReturn {
  isPending: ComputedRef<boolean>;
  isError: ComputedRef<boolean>;
  error: ComputedRef<unknown>;
  isTracksLoading: ComputedRef<boolean>;

  journey: ComputedRef<Journey | undefined>;
  journeyFull: ComputedRef<JourneyFull | undefined>;
  journeyGeoJSON: ComputedRef<GeoJSONFeatureCollection>;
  allModes: ComputedRef<Mode[]>;
  allAnnotations: ComputedRef<Annotation[]>;
  timelineData: ComputedRef<TimelineData>;

  weatherStations: ComputedRef<WeatherStation[]>;
  selectedStationId: WritableComputedRef<number | null>;
  weatherSummary: ComputedRef<WeatherSummary | undefined>;
  hasWeather: ComputedRef<boolean>;

  isEditMode: Ref<boolean>;
  enterEditMode(cursor: TimelineCursorState): void;
  exitEditMode(cursor: TimelineCursorState): void;

  deleteJourney(): void;
  updateJourney(data: Partial<Journey>): void;
}

export function useJourneyDetail(
  journeyId: MaybeRefOrGetter<string | number>,
  options?: { onDeleted?: () => void },
): UseJourneyDetailReturn {
  const queryCache = useQueryCache();

  const { state: journeyState } = useJourneyQuery(journeyId);
  const { state: modesState } = useJourneyModesQuery(journeyId);
  const { state: annotationsState } = useJourneyAnnotationsQuery(journeyId);
  const { state: aboardState } = useJourneyAboardQuery(journeyId);
  const { state: journeyTracksState } = useJourneyTracksQuery(journeyId);
  const { state: weatherStationsState } = useWeatherStationsQuery();
  const { state: weatherSummaryState } = useJourneyWeatherSummaryQuery(journeyId);

  const isPending = computed(() => journeyState.value.status === "pending");
  const isError = computed(() => journeyState.value.status === "error");
  const error = computed(() => journeyState.value.error);
  const isTracksLoading = computed(() => journeyTracksState.value.status === "pending");

  const journey = computed(() => journeyState.value.data);

  const allModes = computed(() => modesState.value.data ?? []);
  const allAnnotations = computed(() => annotationsState.value.data ?? []);
  const weatherStations = computed(() => weatherStationsState.value.data ?? []);
  const weatherSummary = computed(() => weatherSummaryState.value.data);
  const hasWeather = computed(() => (weatherSummary.value?.window.observation_count ?? 0) > 0);

  const journeyFull = computed<JourneyFull | undefined>(() => {
    if (!journey.value) return undefined;
    return {
      journey: journey.value,
      modes: allModes.value,
      annotations: allAnnotations.value,
      aboard: aboardState.value.data ?? [],
    };
  });

  const journeyGeoJSON = computed(() =>
    journeyTracksState.value.status === "success" ? journeyTracksState.value.data : EMPTY_FEATURE_COLLECTION,
  );

  const timelineData = computed<TimelineData>(() => ({
    features: journeyGeoJSON.value,
    modes: allModes.value,
    annotations: allAnnotations.value,
    journeyStart: journey.value?.started_at ?? "1970-01-01T00:00:00.000Z",
    journeyEnd: journey.value?.ended_at ?? null,
  }));

  const isEditMode = ref(false);
  const canEdit = computed(() => journeyState.value.status === "success");

  function enterEditMode(cursor: TimelineCursorState) {
    if (!canEdit.value) return;
    isEditMode.value = true;
    cursor.resetCursor();
  }

  function exitEditMode(cursor: TimelineCursorState) {
    isEditMode.value = false;
    cursor.clearCursor();
  }

  const { mutate: mutateDelete } = useMutation({
    mutation: () => deleteJourney(Number(toValue(journeyId))),
    onSettled: () => {
      queryCache.invalidateQueries({ key: journeyQueryKey(toValue(journeyId)) });
      queryCache.invalidateQueries({ key: ["journeys"] });
      options?.onDeleted?.();
    },
  });

  const { mutate: mutateUpdate } = useMutation({
    mutation: (data: Partial<Journey>) => updateJourney(Number(toValue(journeyId)), data),
    onSettled: (_result, _error, vars) => {
      const id = toValue(journeyId);
      queryCache.invalidateQueries({ key: journeyQueryKey(id) });
      if ("weather_station" in vars) {
        queryCache.invalidateQueries({ key: journeyTracksQueryKey(id) });
        queryCache.invalidateQueries({ key: journeyWeatherSummaryQueryKey(id) });
      }
      if (JOURNEY_LIST_FIELDS.some((f) => f in vars)) {
        queryCache.invalidateQueries({ key: ["journeys"] });
      }
    },
  });

  const selectedStationId = computed({
    get: () => journey.value?.weather_station ?? null,
    set: (id: number | null) => {
      if (journey.value && id !== journey.value.weather_station) {
        mutateUpdate({ weather_station: id });
      }
    },
  });

  return {
    isPending,
    isError,
    error,
    isTracksLoading,
    journey,
    journeyFull,
    journeyGeoJSON,
    allModes,
    allAnnotations,
    timelineData,
    weatherStations,
    selectedStationId,
    weatherSummary,
    hasWeather,
    isEditMode,
    enterEditMode,
    exitEditMode,
    deleteJourney: mutateDelete,
    updateJourney: mutateUpdate,
  };
}
