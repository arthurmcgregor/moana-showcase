import type { MaybeRefOrGetter } from "vue";
import { toValue } from "vue";
import { useQuery } from "@pinia/colada";
import {
  fetchJourney,
  fetchJourneys,
  fetchJourneyModes,
  fetchJourneyAnnotations,
  fetchJourneyAboard,
  fetchJourneyTracks,
  fetchJourneyWeatherSummary,
  fetchJourneyPhotos,
  fetchVessels,
  fetchWeatherStations,
  fetchTracks,
  fetchPeople,
  fetchPerson,
  fetchPersonHistory,
} from "@/api/web";

const FIVE_MINUTES = 1000 * 60 * 5;
const THIRTY_MINUTES = 1000 * 60 * 30;

// ============================================================================
// Journey queries
// ============================================================================

export const journeyQueryKey = (id: string | number) => ["journey", Number(id)];
export const journeyModesQueryKey = (id: string | number) => ["journeyModes", Number(id)];
export const journeyAnnotationsQueryKey = (id: string | number) => ["journeyAnnotations", Number(id)];
export const journeyTracksQueryKey = (id: string | number) => ["journeyTracks", Number(id)];
export const journeyWeatherSummaryQueryKey = (id: string | number) => ["journeyWeatherSummary", Number(id)];
export const journeyPhotosQueryKey = (id: string | number) => ["journeyPhotos", Number(id)];

export function useJourneyQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyQueryKey(toValue(id)),
    query: () => fetchJourney(Number(toValue(id))),
    staleTime: FIVE_MINUTES,
  });
}

export function useJourneyModesQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyModesQueryKey(toValue(id)),
    query: () => fetchJourneyModes(Number(toValue(id))),
    staleTime: FIVE_MINUTES,
  });
}

export function useJourneyAnnotationsQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyAnnotationsQueryKey(toValue(id)),
    query: () => fetchJourneyAnnotations(Number(toValue(id))),
    staleTime: FIVE_MINUTES,
  });
}

export const journeyAboardQueryKey = (id: string | number) => ["journeyAboard", Number(id)];

export function useJourneyAboardQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyAboardQueryKey(toValue(id)),
    query: () => fetchJourneyAboard(Number(toValue(id))),
    staleTime: FIVE_MINUTES,
  });
}

export function useJourneyTracksQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyTracksQueryKey(toValue(id)),
    query: () => fetchJourneyTracks(Number(toValue(id))),
    staleTime: THIRTY_MINUTES,
    gcTime: THIRTY_MINUTES,
  });
}

export function useJourneyPhotosQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyPhotosQueryKey(toValue(id)),
    query: () => fetchJourneyPhotos(Number(toValue(id))),
    staleTime: THIRTY_MINUTES,
    gcTime: THIRTY_MINUTES,
  });
}

export function useJourneyWeatherSummaryQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => journeyWeatherSummaryQueryKey(toValue(id)),
    query: () => fetchJourneyWeatherSummary(Number(toValue(id))),
    staleTime: THIRTY_MINUTES,
    gcTime: THIRTY_MINUTES,
  });
}

// ============================================================================
// List queries
// ============================================================================

export function useJourneysQuery() {
  return useQuery({
    key: ["journeys"],
    query: async () => {
      const journeys = await fetchJourneys();
      return [...journeys.results].sort(
        (a, b) => new Date(b.started_at).getTime() - new Date(a.started_at).getTime(),
      );
    },
    staleTime: FIVE_MINUTES,
  });
}

export function useVesselsQuery() {
  return useQuery({
    key: ["vessels"],
    query: async () => {
      const vessels = await fetchVessels();
      return vessels.results;
    },
    staleTime: FIVE_MINUTES,
  });
}

export function useWebTracksQuery() {
  return useQuery({
    key: ["webTracks"],
    query: async () => {
      const tracks = await fetchTracks({ ordering: "-started_at" });
      return tracks.results;
    },
    staleTime: FIVE_MINUTES,
  });
}

export function useWeatherStationsQuery() {
  return useQuery({
    key: ["weatherStations"],
    query: () => fetchWeatherStations(),
    staleTime: THIRTY_MINUTES,
  });
}

// ============================================================================
// People queries
// ============================================================================

export const peopleQueryKey = () => ["people"];
export const personQueryKey = (id: string | number) => ["person", Number(id)];
export const personHistoryQueryKey = (id: string | number) => ["personHistory", Number(id)];

export function usePeopleQuery() {
  return useQuery({
    key: peopleQueryKey,
    query: async () => {
      const response = await fetchPeople();
      return response.results;
    },
    staleTime: FIVE_MINUTES,
  });
}

export function usePersonQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => personQueryKey(toValue(id)),
    query: () => fetchPerson(Number(toValue(id))),
    staleTime: FIVE_MINUTES,
  });
}

export function usePersonHistoryQuery(id: MaybeRefOrGetter<string | number>) {
  return useQuery({
    key: () => personHistoryQueryKey(toValue(id)),
    query: () => fetchPersonHistory(Number(toValue(id))),
    staleTime: FIVE_MINUTES,
  });
}
