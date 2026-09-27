<script setup lang="ts">
import { ref, computed } from "vue";
import { useRoute, useRouter } from "vue-router";
import { useQuery, useMutation, useQueryCache } from "@pinia/colada";
import { PaperAirplaneIcon, MapIcon } from "@heroicons/vue/24/outline";
import type { TravelMode } from "@/api/types";
import {
  fetchTrack,
  fetchWebTrackGeoJson,
  deleteTrack,
  createJourney,
  updateJourney,
  type Journey,
  type GeoJSONFeatureCollection,
} from "@/api/web";
import { useAuthStore } from "@/stores";
import { useJourneysQuery } from "@/composables/useQueries";

const authStore = useAuthStore();
const route = useRoute();
const router = useRouter();
const queryCache = useQueryCache();

const activeTab = ref<"map" | "graph">("map");
const travelMode = ref<TravelMode>("sailing");
const showJourneyModal = ref(false);

const { state: trackState } = useQuery({
  key: () => ["track", route.params.id],
  query: () => fetchTrack(Number(route.params.id as string)),
  staleTime: 1000 * 60 * 5,
});

const { state: trackGeoJsonState } = useQuery({
  key: () => ["trackGeoJson", route.params.id],
  query: () => fetchWebTrackGeoJson(Number(route.params.id as string)),
  staleTime: 1000 * 60 * 30,
  gcTime: 1000 * 60 * 30,
});

const { state: journeysState } = useJourneysQuery();

const { mutate: deleteTrackMutate, isLoading: deleteLoading } = useMutation({
  mutation: () => deleteTrack(Number(route.params.id as string)),
  onSuccess: () => {
    queryCache.invalidateQueries({ key: ["webTracks"] });
    queryCache.invalidateQueries({ key: ["track", route.params.id] });
    queryCache.invalidateQueries({ key: ["trackGeoJson", route.params.id] });
    router.push("/tracks/web");
  },
});

const { mutate: createJourneyMutate, isLoading: createLoading, error: createError } = useMutation({
  mutation: (params: Parameters<typeof createJourney>[0]) => createJourney(params),
  onSuccess: (journey) => {
    queryCache.invalidateQueries({ key: ["journeys"] });
    showJourneyModal.value = false;
    router.push(`/journeys/${journey.id}`);
  },
});

const { mutate: addToJourneyMutate, isLoading: addLoading, error: addError } = useMutation({
  mutation: ({
    journey,
    track,
  }: {
    journey: Journey;
    track: NonNullable<typeof trackState.value.data>;
  }) => {
    const newStart = new Date(
      Math.min(
        new Date(journey.started_at).getTime(),
        new Date(track.started_at).getTime(),
      ),
    ).toISOString();
    const newEnd = new Date(
      Math.max(
        new Date(journey.ended_at ?? journey.started_at).getTime(),
        new Date(track.ended_at ?? track.started_at).getTime(),
      ),
    ).toISOString();
    const appendedNote = `Created from track: ${track.name}`;
    const newNotes = journey.notes ? `${journey.notes}\n${appendedNote}` : appendedNote;
    return updateJourney(journey.id, {
      started_at: newStart,
      ended_at: newEnd,
      notes: newNotes,
    });
  },
  onSuccess: (_, vars) => {
    queryCache.invalidateQueries({ key: ["journeys"] });
    showJourneyModal.value = false;
    router.push(`/journeys/${vars.journey.id}`);
  },
});

const journeyLoading = computed(() => createLoading.value || addLoading.value);
const journeyError = computed(() => createError.value ?? addError.value);

const trackMarkers = computed(
  () =>
    (trackGeoJsonState.value.data?.markers ?? {
      type: "FeatureCollection",
      features: [],
    }) as GeoJSONFeatureCollection,
);

const currentJourneys = computed(() => {
  const track = trackState.value.data;
  if (!track?.ended_at || journeysState.value.status !== "success") return [];
  const trackStart = new Date(track.started_at).getTime();
  const trackEnd = new Date(track.ended_at).getTime();
  return journeysState.value.data.filter((j: Journey) => {
    const journeyStart = new Date(j.started_at).getTime();
    const journeyEnd = j.ended_at ? new Date(j.ended_at).getTime() : journeyStart;
    return trackStart <= journeyEnd && trackEnd >= journeyStart;
  });
});

const journeys = computed(() => {
  if (journeysState.value.status !== "success") return [];
  return [...journeysState.value.data].sort(
    (a: Journey, b: Journey) =>
      new Date(b.started_at).getTime() - new Date(a.started_at).getTime(),
  );
});

function onCreateJourney(payload: { name: string; description: string; notes: string }) {
  const track = trackState.value.data;
  if (!track || track.vessel == null) return;
  createJourneyMutate({
    vessel: track.vessel,
    name: payload.name,
    description: payload.description,
    notes: payload.notes,
    started_at: track.started_at,
    ended_at: track.ended_at ?? track.started_at,
  });
}

function onAddToJourney({ journeyId }: { journeyId: number }) {
  const track = trackState.value.data;
  if (!track) return;
  const journey = journeys.value.find((j: Journey) => j.id === journeyId);
  if (!journey) return;
  addToJourneyMutate({ journey, track });
}
</script>

<template>
  <div
    v-if="trackGeoJsonState.status === 'pending' || trackState.status === 'pending'"
    class="flex flex-1 items-center justify-center"
  >
    <div class="loading loading-xl loading-spinner" />
  </div>

  <div
    v-else-if="trackGeoJsonState.status === 'error' || trackState.status === 'error'"
    role="alert"
    class="alert alert-error"
  >
    <OutlineXCircleIcon class="size-6 shrink-0 stroke-current" />
    <span>{{ (trackGeoJsonState.error ?? trackState.error) }}</span>
  </div>

  <div
    v-else-if="trackGeoJsonState.status === 'success'"
    class="flex min-h-0 flex-1 flex-col"
  >
    <!-- Header -->
    <div class="mb-2 flex flex-wrap items-center justify-between gap-2 md:gap-4">
      <div class="flex min-w-0 flex-1 items-center gap-2 md:gap-3">
        <button
          class="btn btn-ghost btn-sm"
          @click="router.push('/tracks/web')"
        >
          <OutlineArrowUturnLeftIcon class="size-5" />
        </button>
        <h2 class="truncate font-display text-2xl font-normal tracking-tight text-base-content md:text-3xl">
          {{ trackState.data?.name }}
        </h2>
      </div>
      <div class="flex flex-wrap items-center justify-end gap-2 basis-full md:basis-auto">
        <IconSelect
          v-model="travelMode"
          :icon="travelMode === 'sailing' ? PaperAirplaneIcon : MapIcon"
          title="Travel mode"
          :max-width="140"
        >
          <option value="sailing">
            Sailing
          </option>
          <option value="walking">
            Walking
          </option>
          <option value="biking">
            Biking
          </option>
          <option value="driving">
            Driving
          </option>
        </IconSelect>
        <template v-if="authStore.isAdmin">
          <button
            type="button"
            class="btn-journey"
            title="Add to journey"
            @click="showJourneyModal = true"
          >
            <OutlinePlusIcon class="size-4" />
            <span>Journey</span>
          </button>
          <ActionGroup>
            <ActionBtn
              variant="danger"
              title="Delete track"
              onclick="delete_track.showModal()"
            >
              <OutlineTrashIcon class="size-5" />
              <span class="sr-only">Delete</span>
            </ActionBtn>
          </ActionGroup>
        </template>
      </div>
    </div>

    <!-- Part-of-journey chips -->
    <div
      v-if="currentJourneys.length > 0"
      class="mb-4 flex flex-wrap items-center gap-2"
    >
      <span class="font-mono text-[11px] uppercase tracking-wider text-base-content/40">
        Part of
      </span>
      <JourneyRef
        v-for="j in currentJourneys"
        :key="j.id"
        :to="`/journeys/${j.id}`"
        :name="j.name"
      />
    </div>

    <!-- Delete modal -->
    <dialog
      id="delete_track"
      class="modal"
    >
      <div class="modal-box w-[calc(100vw-1rem)] max-h-[calc(100dvh-2rem)] overflow-y-auto">
        <h3 class="text-lg font-bold">
          Delete Track: {{ trackState.data?.name }}
        </h3>
        <p class="py-4">
          Are you sure you want to delete this track? Cannot be undone.
        </p>
        <div class="modal-action flex-wrap gap-2">
          <form method="dialog">
            <button class="btn">
              Cancel
            </button>
            <button
              class="btn btn-error"
              :disabled="deleteLoading"
              @click="deleteTrackMutate()"
            >
              <span
                v-if="deleteLoading"
                class="loading loading-spinner loading-sm"
              />
              {{ deleteLoading ? "Deleting..." : "Delete" }}
            </button>
          </form>
        </div>
      </div>
    </dialog>

    <!-- Tabs -->
    <div class="flex min-h-0 flex-1 flex-col">
      <nav
        role="tablist"
        class="tab-scroll flex gap-1 border-b border-base-300 px-1"
      >
        <button
          v-for="tab in ([
            { key: 'map', label: 'Map' },
            { key: 'graph', label: 'Speed' },
          ] as const)"
          :key="tab.key"
          role="tab"
          :aria-selected="activeTab === tab.key"
          class="relative shrink-0 whitespace-nowrap px-3 py-2 font-mono text-xs uppercase tracking-wider transition-colors"
          :class="activeTab === tab.key
            ? 'text-base-content'
            : 'text-base-content/40 hover:text-base-content/70'"
          @click="activeTab = tab.key"
        >
          {{ tab.label }}
          <span
            v-if="activeTab === tab.key"
            class="absolute inset-x-0 -bottom-px h-0.5 bg-accent"
          />
        </button>
      </nav>

      <div
        class="flex min-h-0 flex-1 flex-col overflow-hidden bg-base-100"
      >
        <!-- Map tab -->
        <div
          v-show="activeTab === 'map'"
          class="min-h-0 flex-1"
        >
          <TheMap
            :markers="trackMarkers"
            :travel-mode="travelMode"
          />
        </div>

        <!-- Speed Graph tab -->
        <div
          v-if="activeTab === 'graph'"
          class="min-h-0 flex-1 p-4"
        >
          <SpeedBarGraph
            :journey-tracks="trackMarkers"
            :travel-mode="travelMode"
          />
        </div>
      </div>
    </div>

    <JourneyCreateModal
      :show="showJourneyModal"
      :track="trackState.data ?? null"
      :journeys="journeys"
      :current-journeys="currentJourneys"
      :submitting="journeyLoading"
      :error="journeyError"
      @cancel="showJourneyModal = false"
      @create-journey="onCreateJourney"
      @add-to-journey="onAddToJourney"
    />
  </div>
</template>

<style scoped>
.btn-journey {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 32px;
  padding: 0 14px 0 12px;
  font-size: 13px;
  font-weight: 600;
  color: var(--color-primary);
  background: color-mix(in oklch, var(--color-primary) 12%, transparent);
  border: 1px solid color-mix(in oklch, var(--color-primary) 30%, transparent);
  border-radius: 8px;
  cursor: pointer;
  transition: background-color 120ms ease, border-color 120ms ease, transform 120ms ease;
}

.btn-journey:hover {
  background: color-mix(in oklch, var(--color-primary) 22%, transparent);
  border-color: color-mix(in oklch, var(--color-primary) 50%, transparent);
}

.btn-journey:active {
  transform: translateY(1px);
}
</style>
