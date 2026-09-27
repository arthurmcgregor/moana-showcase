<script setup lang="ts">
import { type Journey } from "@/api/web";
import { config } from "@/config";
import { useJourneysQuery } from "@/composables/useQueries";
import { useVesselName } from "@/composables/useVesselName";
import { journeyMeta } from "@/utils/listHelpers";

function journeyPreviewUrl(journeyId: number, zoom: "region" | "detail" = "region") {
  return `${config.webServerBaseUrl}/api/tracks/journeys/${journeyId}/preview/?zoom=${zoom}`;
}

const { state: journeyState } = useJourneysQuery();
const vesselName = useVesselName();
</script>

<template>
  <div>
    <PageHeader title="Journeys" />

    <QueryStateWrapper
      :state="journeyState"
      loading-label="Loading journeys…"
      :empty="{ eyebrow: 'All quiet', title: 'No journeys yet.', subtitle: 'Record a voyage to see it here.' }"
    >
      <template #default="{ data }">
        <ul class="list rounded-box bg-base-100 shadow-md">
          <ListItem
            v-for="journey in (data as Journey[])"
            :key="journey.id"
            :to="`/journeys/${journey.id}`"
            :title="journey.name"
            :description="journey.description"
            :meta="journeyMeta(journey, vesselName)"
          >
            <template #media>
              <HoverSwapPreview
                :region-src="journeyPreviewUrl(journey.id, 'region')"
                :detail-src="journeyPreviewUrl(journey.id, 'detail')"
              />
            </template>
          </ListItem>
        </ul>
      </template>
    </QueryStateWrapper>
  </div>
</template>
