<script setup lang="ts">
import { computed } from "vue";
import { useRoute, useRouter } from "vue-router";
import { useJourneyPhotosQuery } from "@/composables/useQueries";
import type { JourneyPhotosResponse } from "@/api/web";

const props = defineProps<{
  journeyId: number | string;
}>();

const route = useRoute();
const router = useRouter();

const { state, refresh } = useJourneyPhotosQuery(() => props.journeyId);

function openAsset(id: string) {
  router.replace({ query: { ...route.query, tab: "photos", asset: id } });
}

function formatWindow(window: JourneyPhotosResponse["window"]): string {
  if (!window) return "";
  const fmt = (iso: string) =>
    new Date(iso).toLocaleString(undefined, {
      dateStyle: "medium",
      timeStyle: "short",
    });
  return `${fmt(window.start)} – ${fmt(window.end)}`;
}

const emptySubtitle = computed(() => {
  const window = (state.value.data as JourneyPhotosResponse | undefined)?.window;
  const range = formatWindow(window ?? null);
  return range
    ? `No images in Immich were taken between ${range} (journey time ±2h).`
    : `No images in Immich were taken during this journey's time window (±2h).`;
});
</script>

<template>
  <div
    v-if="state.status === 'error'"
    class="flex min-h-0 flex-1 items-center justify-center p-6"
  >
    <AppNotice variant="error">
      <div class="flex items-center justify-between gap-4">
        <span>Couldn't load photos from Immich.</span>
        <BtnText @click="refresh()">
          Retry
        </BtnText>
      </div>
    </AppNotice>
  </div>

  <QueryStateWrapper
    v-else
    :state="state"
    loading-label="Loading photos…"
    :is-empty="(data: JourneyPhotosResponse) => data.photos.length === 0"
    :empty="{
      eyebrow: 'Photos',
      title: 'No photos for this journey',
      subtitle: emptySubtitle,
    }"
  >
    <template #default="{ data }">
      <div class="flex min-h-0 flex-1 flex-col overflow-auto p-6">
        <div class="grid grid-cols-2 gap-3 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5">
          <button
            v-for="photo in (data as JourneyPhotosResponse).photos"
            :key="photo.id"
            class="group relative aspect-square overflow-hidden rounded bg-base-200"
            @click="openAsset(photo.id)"
          >
            <img
              :src="photo.thumbUrl"
              :alt="photo.takenAt ?? ''"
              loading="lazy"
              class="h-full w-full object-cover transition-transform group-hover:scale-105"
            >
            <div
              v-if="photo.type === 'VIDEO'"
              class="pointer-events-none absolute inset-0 flex items-center justify-center bg-black/20"
            >
              <SolidPlayCircleIcon class="size-12 text-white drop-shadow-lg" />
            </div>
          </button>
        </div>
      </div>
    </template>
  </QueryStateWrapper>
</template>
