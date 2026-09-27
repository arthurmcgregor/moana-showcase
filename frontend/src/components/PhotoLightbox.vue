<script setup lang="ts">
import { computed, onMounted, onBeforeUnmount } from "vue";
import type { JourneyPhoto } from "@/api/web";

const props = defineProps<{
  photos: JourneyPhoto[];
  startIndex: number;
}>();

const emit = defineEmits<{
  close: [];
  navigate: [assetId: string];
}>();

const current = computed(() => props.photos[props.startIndex]);

function step(delta: number) {
  const len = props.photos.length;
  if (len === 0) return;
  const next = (props.startIndex + delta + len) % len;
  emit("navigate", props.photos[next].id);
}

function onKey(e: KeyboardEvent) {
  if (e.key === "Escape") emit("close");
  else if (e.key === "ArrowLeft") step(-1);
  else if (e.key === "ArrowRight") step(1);
}

onMounted(() => {
  window.addEventListener("keydown", onKey);
  document.body.style.overflow = "hidden";
});

onBeforeUnmount(() => {
  window.removeEventListener("keydown", onKey);
  document.body.style.overflow = "";
});

const formattedTaken = computed(() =>
  current.value?.takenAt ? new Date(current.value.takenAt).toLocaleString() : "",
);
</script>

<template>
  <Teleport to="body">
    <div
      class="fixed inset-0 z-50 flex items-center justify-center bg-black/90"
      role="dialog"
      aria-modal="true"
      @click.self="emit('close')"
    >
      <button
        class="btn btn-circle btn-ghost absolute right-4 top-4 text-white hover:bg-white/10"
        aria-label="Close"
        @click="emit('close')"
      >
        <OutlineXMarkIcon class="size-6" />
      </button>

      <button
        v-if="photos.length > 1"
        class="btn btn-circle btn-ghost absolute left-4 text-white hover:bg-white/10"
        aria-label="Previous photo"
        @click="step(-1)"
      >
        <OutlineChevronLeftIcon class="size-6" />
      </button>

      <button
        v-if="photos.length > 1"
        class="btn btn-circle btn-ghost absolute right-4 text-white hover:bg-white/10"
        aria-label="Next photo"
        @click="step(1)"
      >
        <OutlineChevronRightIcon class="size-6" />
      </button>

      <div
        v-if="current"
        class="flex max-h-[90vh] max-w-[90vw] flex-col items-center gap-3"
      >
        <template v-if="current.type === 'VIDEO'">
          <a
            :href="current.immichUrl ?? undefined"
            target="_blank"
            rel="noopener"
            class="group relative block"
            :class="{ 'pointer-events-none': !current.immichUrl }"
          >
            <img
              :src="current.previewUrl"
              :alt="formattedTaken"
              class="max-h-[80vh] max-w-[90vw] rounded object-contain"
            >
            <div class="pointer-events-none absolute inset-0 flex flex-col items-center justify-center gap-3 bg-black/30 transition-opacity group-hover:bg-black/10">
              <OutlinePlayCircleIcon class="size-24 text-white drop-shadow-lg" />
              <span class="rounded bg-black/60 px-3 py-1 font-mono text-xs uppercase tracking-wider text-white">
                Open in Immich to play
              </span>
            </div>
          </a>
        </template>
        <template v-else>
          <img
            :src="current.previewUrl"
            :alt="formattedTaken"
            class="max-h-[80vh] max-w-[90vw] rounded object-contain"
          >
        </template>

        <div class="flex items-center gap-4 font-mono text-xs uppercase tracking-wider text-white/80">
          <span>{{ formattedTaken }}</span>
          <span>{{ startIndex + 1 }} / {{ photos.length }}</span>
          <a
            v-if="current.immichUrl && current.type !== 'VIDEO'"
            :href="current.immichUrl"
            target="_blank"
            rel="noopener"
            class="underline hover:text-white"
          >
            Open in Immich
          </a>
        </div>
      </div>
    </div>
  </Teleport>
</template>
