<script setup lang="ts">
import { computed } from "vue";
import { type Track, type Journey } from "@/api/web";
import { config } from "@/config";
import { useWebTracksQuery, useJourneysQuery } from "@/composables/useQueries";
import { useVesselName } from "@/composables/useVesselName";
import { trackMeta } from "@/utils/listHelpers";

function trackPreviewUrl(trackId: number, zoom: "region" | "detail" = "region") {
  return `${config.webServerBaseUrl}/api/tracks/tracks/${trackId}/preview/?zoom=${zoom}`;
}

const { state: tracksState } = useWebTracksQuery();
const { state: journeysState } = useJourneysQuery();
const vesselName = useVesselName();

const journeysWithRange = computed(() => {
  if (journeysState.value.status !== "success") return [];
  return journeysState.value.data.map((j: Journey) => {
    const start = new Date(j.started_at).getTime();
    const end = j.ended_at ? new Date(j.ended_at).getTime() : start;
    return { journey: j, start, end };
  });
});

const trackJourneyMap = computed(() => {
  if (tracksState.value.status !== "success") return new Map<number, Journey>();
  const ranges = journeysWithRange.value;
  const result = new Map<number, Journey>();
  for (const track of tracksState.value.data) {
    if (!track.ended_at) continue;
    const tStart = new Date(track.started_at).getTime();
    const tEnd = new Date(track.ended_at).getTime();
    const match = ranges.find(({ start, end }) => tStart <= end && tEnd >= start);
    if (match) result.set(track.id, match.journey);
  }
  return result;
});
</script>

<template>
  <div>
    <PageHeader title="Tracks" />

    <QueryStateWrapper
      :state="tracksState"
      loading-label="Loading tracks…"
      :empty="{ eyebrow: 'All quiet', title: 'No tracks yet.', subtitle: 'Upload a track from your device to get started.' }"
    >
      <template #default="{ data }">
        <ul class="list rounded-box bg-base-100 shadow-md">
          <ListItem
            v-for="track in (data as Track[])"
            :key="track.id"
            :to="{ name: 'track-view', params: { id: track.id.toString() } }"
            :title="track.name"
            :description="track.description"
            :meta="trackMeta(track, vesselName)"
          >
            <template #media>
              <HoverSwapPreview
                :region-src="trackPreviewUrl(track.id, 'region')"
                :detail-src="trackPreviewUrl(track.id, 'detail')"
              />
            </template>
            <template
              v-if="trackJourneyMap.get(track.id)"
              #cross-ref
            >
              <router-link
                :to="`/journeys/${trackJourneyMap.get(track.id)!.id}`"
                class="badge badge-soft badge-primary text-xs"
                @click.stop
              >
                {{ trackJourneyMap.get(track.id)!.name }}
              </router-link>
            </template>
          </ListItem>
        </ul>
      </template>
    </QueryStateWrapper>
  </div>
</template>
