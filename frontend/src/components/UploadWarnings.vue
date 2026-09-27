<script setup lang="ts">
import { computed } from "vue";
import type { UploadTrackCsvResponse } from "@/api/web";

// What an upload came back with that the server kept the track for but could not
// use: a CSV row it could not parse, a sidecar line timestamped outside the
// track, a mis-press burst it collapsed. The response is the only place these
// exist — the track view has no idea anything was skipped and the device list's
// "Uploaded" pill even less — so both upload views hold their page and show this
// rather than routing away (issue #156).

const props = defineProps<{ result: UploadTrackCsvResponse }>();

// One sentence rather than three nested templates in the markup.
const summary = computed(() => {
  const { points_created: points, modes_created: modes } = props.result;
  const withModes
    = modes === undefined ? "" : ` and ${modes} mode${modes === 1 ? "" : "s"}`;
  return `Track saved with ${points} points${withModes}, but some rows were skipped:`;
});
</script>

<template>
  <AppNotice variant="warning">
    <p>{{ summary }}</p>
    <ul class="warning-list">
      <li
        v-for="(warning, i) in result.warnings"
        :key="i"
      >
        {{ warning }}
      </li>
    </ul>
  </AppNotice>
</template>

<style scoped>
.warning-list {
  margin: 6px 0 0;
  padding-left: 18px;
  list-style: disc;
  font-weight: 400;
}
</style>
