<script setup lang="ts">
import { ref, watchEffect } from "vue";
import { useRouter } from "vue-router";
import { useMutation, useQueryCache } from "@pinia/colada";
import { ArrowUpTrayIcon } from "@heroicons/vue/24/outline";
import { useAuthStore } from "@/stores";
import { uploadTrackCsv, type UploadTrackCsvResponse, type Vessel } from "@/api/web";
import { useVesselsQuery } from "@/composables/useQueries";

const router = useRouter();
const authStore = useAuthStore();
const queryCache = useQueryCache();

const { state: vesselsState } = useVesselsQuery();

// An upload that produced warnings holds the page instead of routing away —
// `UploadWarnings` says why. Cleared when the next upload starts, so the panel
// never describes a run other than the one that just finished.
const uploadedWithWarnings = ref<UploadTrackCsvResponse | null>(null);

const { mutate: upload, isLoading: uploading, error } = useMutation({
  mutation: ({ file, name, vessel, events }: { file: File; name: string; vessel: number; events?: File }) =>
    uploadTrackCsv(file, { name, vessel }, events),
  onSuccess: (track) => {
    queryCache.invalidateQueries({ key: ["webTracks"] });
    if (track.warnings?.length) {
      uploadedWithWarnings.value = track;
      return;
    }
    router.push({ name: "track-view", params: { id: track.track_id } });
  },
});

const selectedFile = ref<File | null>(null);
const eventsFile = ref<File | null>(null);
const trackName = ref("");
const selectedVessel = ref<number | null>(null);

watchEffect(() => {
  if (selectedVessel.value === null && vesselsState.value.status === "success" && vesselsState.value.data.length === 1) {
    selectedVessel.value = vesselsState.value.data[0].id;
  }
});

function onFileSelect(file: File) {
  selectedFile.value = file;
  if (!trackName.value) {
    trackName.value = file.name.replace(/\.csv$/i, "");
  }
}

function handleUpload() {
  if (!selectedFile.value || !trackName.value || !selectedVessel.value) return;
  uploadedWithWarnings.value = null;
  upload({
    file: selectedFile.value,
    name: trackName.value,
    vessel: selectedVessel.value,
    events: eventsFile.value ?? undefined,
  });
}
</script>

<template>
  <div class="mx-auto w-full max-w-xl">
    <PageHeader
      title="Upload Local Track"
      subtitle="Upload a CSV file from your computer to create a track"
    />

    <AppNotice
      v-if="authStore.isLoaded && !authStore.isAdmin"
      variant="info"
    >
      You have view-only access. Track uploads require admin permissions.
    </AppNotice>

    <form
      v-else
      class="panel"
      @submit.prevent="handleUpload"
    >
      <AppField label="CSV File">
        <AppDropzone
          v-if="!selectedFile"
          accept=".csv,text/csv"
          :icon="ArrowUpTrayIcon"
          primary="Drop a CSV file here"
          @select="onFileSelect"
        />
        <AppFileChip
          v-else
          :file="selectedFile"
          @remove="selectedFile = null"
        />
      </AppField>

      <AppField
        label="Watch Events"
        hint="Optional — the .events.jsonl sidecar recorded next to the CSV"
      >
        <AppDropzone
          v-if="!eventsFile"
          accept=".jsonl,application/x-ndjson"
          primary="Drop an events file here"
          secondary="or click to browse — skip if you don't have one"
          @select="eventsFile = $event"
        />
        <AppFileChip
          v-else
          :file="eventsFile"
          @remove="eventsFile = null"
        />
      </AppField>

      <AppField label="Track Name">
        <input
          v-model="trackName"
          type="text"
          class="ui-input"
          placeholder="Enter a name for this track"
        >
      </AppField>

      <AppField label="Vessel">
        <AppSelect v-model="selectedVessel">
          <option
            :value="null"
            disabled
          >
            Select a vessel
          </option>
          <option
            v-for="vessel in (vesselsState.status === 'success' ? vesselsState.data : [] as Vessel[])"
            :key="vessel.id"
            :value="vessel.id"
          >
            {{ vessel.name }}
          </option>
        </AppSelect>
      </AppField>

      <AppNotice
        v-if="error"
        variant="error"
      >
        {{ error }}
      </AppNotice>

      <UploadWarnings
        v-if="uploadedWithWarnings"
        :result="uploadedWithWarnings"
      />

      <div class="actions">
        <!-- With warnings up the ghost button dismisses them rather than leaving
             the page: the submit button comes back with it, so a corrected file
             can be uploaded without walking back in from the menu. -->
        <BtnText
          type="button"
          @click="uploadedWithWarnings ? (uploadedWithWarnings = null) : router.push('/')"
        >
          {{ uploadedWithWarnings ? "Done" : "Cancel" }}
        </BtnText>
        <BtnSave
          v-if="!uploadedWithWarnings"
          :loading="uploading"
          :disabled="!selectedFile || !trackName || !selectedVessel"
        >
          {{ uploading ? "Uploading…" : "Upload Track" }}
        </BtnSave>
        <BtnSave
          v-else
          type="button"
          @click="router.push({ name: 'track-view', params: { id: uploadedWithWarnings.track_id } })"
        >
          View Track
        </BtnSave>
      </div>
    </form>
  </div>
</template>

<style scoped>
.panel {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 24px;
  background: var(--color-base-100);
  border: 1px solid var(--color-base-300);
  border-radius: 14px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.1);
}

.actions {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
  padding-top: 4px;
}
</style>
