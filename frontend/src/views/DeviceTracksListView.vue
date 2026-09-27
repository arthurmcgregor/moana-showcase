<script setup lang="ts">
import { computed, ref } from "vue";
import { useRouter } from "vue-router";
import { useQuery, useMutation, useQueryCache } from "@pinia/colada";
import { useAuthStore } from "@/stores";
import { fetchTrackList, fetchTrackCsv, fetchTrackEvents, eventSidecarName, isNetworkError, isNoSdCardError, type DeviceTrack } from "@/api/device";
import { uploadTrackCsv, type UploadTrackCsvResponse, type Vessel } from "@/api/web";
import { useVesselsQuery, useWebTracksQuery } from "@/composables/useQueries";
import { formatFileSize } from "@/utils/formatters";

const router = useRouter();
const authStore = useAuthStore();
const queryCache = useQueryCache();

const { state: deviceTracksState, refresh: refreshDeviceTracks } = useQuery({
  key: ["deviceTracks"],
  query: fetchTrackList,
});

// Map device fetch failures to a friendly, retryable prompt. A dropped WiFi
// link reads as unreachable; a 500 means the device has no SD card inserted.
// Anything else falls through to the raw error alert.
function deviceTracksError(error: unknown) {
  if (isNetworkError(error)) {
    return {
      eyebrow: "No device",
      title: "Device unreachable.",
      subtitle: "Connect to your device's WiFi to see tracks here.",
    };
  }
  if (isNoSdCardError(error)) {
    return {
      eyebrow: "No SD card",
      title: "Device can't read its storage.",
      subtitle: "Insert an SD card into the device, then try again.",
    };
  }
  return null;
}

const { state: vesselsState } = useVesselsQuery();

// Which device files already exist in the web database, matched by the
// original device filename stored on each uploaded track. Failures/loading
// resolve to an empty set so the device list never blocks on this query.
//
// Device filenames from /tracks/list carry a leading slash (e.g.
// "/gps_...csv"), but the backend stores source_filename as the basename
// (Django's MultiPartParser strips the path), so normalise both sides.
const stripSlash = (name: string) => name.replace(/^\/+/, "");
const { state: webTracksState } = useWebTracksQuery();
const uploadedFilenames = computed(() => {
  const tracks = webTracksState.value.status === "success" ? webTracksState.value.data : [];
  return new Set(tracks.map((t) => stripSlash(t.source_filename)).filter(Boolean));
});
function isUploaded(filename: string): boolean {
  return uploadedFilenames.value.has(stripSlash(filename));
}

const selectedTrack = ref<string | null>(null);
const trackName = ref("");
const selectedVessel = ref<number | null>(null);

// An upload that produced warnings holds this panel instead of routing away —
// `UploadWarnings` says why. Cleared when the next upload starts, so the panel
// never describes a run other than the one that just finished.
const uploadedWithWarnings = ref<UploadTrackCsvResponse | null>(null);

const { mutate: upload, isLoading: uploading, error: uploadError } = useMutation({
  mutation: async ({ file, name, vessel, events }: { file: File; name: string; vessel: number; events?: File }) => {
    return uploadTrackCsv(file, { name, vessel }, events);
  },
  onSuccess: (track) => {
    queryCache.invalidateQueries({ key: ["webTracks"] });
    if (track.warnings?.length) {
      uploadedWithWarnings.value = track;
      return;
    }
    router.push({ name: "track-view", params: { id: track.track_id } });
  },
});

function selectTrack(filename: string) {
  selectedTrack.value = filename;
  trackName.value = filename.replace(/\.csv$/i, "");
}

async function handleUpload() {
  if (!selectedTrack.value || !trackName.value || !selectedVessel.value) return;
  uploadedWithWarnings.value = null;
  const csv = await fetchTrackCsv(selectedTrack.value);
  const file = new File([csv], selectedTrack.value, { type: "text/csv" });
  // Watch-events sidecar rides along when the device has one; null (the normal
  // case for tracks recorded without the watch) means a plain CSV upload.
  const eventsText = await fetchTrackEvents(selectedTrack.value);
  const events = eventsText === null
    ? undefined
    : new File([eventsText], eventSidecarName(selectedTrack.value), { type: "application/x-ndjson" });
  upload({ file, name: trackName.value, vessel: selectedVessel.value, events });
}

</script>

<template>
  <div class="max-w-2xl mx-auto">
    <PageHeader
      title="Upload from Device"
      subtitle="Select a track from your connected device to save it"
    />

    <QueryStateWrapper
      :state="deviceTracksState"
      loading-label="Loading device tracks…"
      :empty="{ eyebrow: 'All quiet', title: 'No tracks on device.', subtitle: 'Start recording a voyage to see it here.' }"
      :recoverable-error="deviceTracksError"
      :on-retry="refreshDeviceTracks"
    >
      <template #default="{ data }">
        <ul class="list rounded-box bg-base-100 shadow-md mb-6">
          <li
            v-for="track in (data as DeviceTrack[])"
            :key="track.name"
            class="list-row hover:cursor-pointer hover:outline-2 hover:outline-base-content/40 font-mono"
            :class="{ 'outline-2 outline-primary bg-primary/5': selectedTrack === track.name }"
            @click="selectTrack(track.name)"
          >
            <div class="list-col-grow">
              {{ track.name }}
            </div>
            <span
              v-if="isUploaded(track.name)"
              class="badge-uploaded self-center"
              title="Already uploaded to the database"
            >
              <SolidCheckCircleIcon class="size-3.5" />
              Uploaded
            </span>
            <span
              v-if="track.size != null"
              class="self-center text-xs text-base-content/50 tabular-nums"
            >
              {{ formatFileSize(track.size) }}
            </span>
            <OutlineChevronRightIcon class="size-5 text-base-content/40 self-center" />
          </li>
        </ul>
      </template>
    </QueryStateWrapper>

    <div
      v-if="selectedTrack && authStore.isAdmin"
      class="card bg-base-100 border border-base-300"
    >
      <div class="card-body gap-4">
        <h3 class="card-title text-base">
          Save "{{ selectedTrack }}"
        </h3>

        <div class="form-control">
          <label class="label"><span class="label-text">Track name</span></label>
          <input
            v-model="trackName"
            type="text"
            class="input input-bordered"
            placeholder="Enter a name"
          >
        </div>

        <div class="form-control">
          <label class="label"><span class="label-text">Vessel</span></label>
          <select
            v-model="selectedVessel"
            class="select select-bordered"
          >
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
          </select>
        </div>

        <AppNotice
          v-if="uploadError"
          variant="error"
        >
          {{ uploadError }}
        </AppNotice>

        <UploadWarnings
          v-if="uploadedWithWarnings"
          :result="uploadedWithWarnings"
        />

        <div class="card-actions justify-end">
          <button
            class="btn btn-ghost"
            @click="selectedTrack = null; uploadedWithWarnings = null"
          >
            {{ uploadedWithWarnings ? "Done" : "Cancel" }}
          </button>
          <button
            v-if="uploadedWithWarnings"
            class="btn btn-primary"
            @click="router.push({ name: 'track-view', params: { id: uploadedWithWarnings.track_id } })"
          >
            View Track
          </button>
          <button
            v-else
            class="btn btn-primary"
            :disabled="!trackName || !selectedVessel || uploading"
            @click="handleUpload"
          >
            <span
              v-if="uploading"
              class="loading loading-spinner loading-sm"
            />
            {{ uploading ? "Uploading..." : "Save to Tracks" }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.badge-uploaded {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  font-family: var(--font-mono, ui-monospace, monospace);
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--color-primary);
  background: color-mix(in oklch, var(--color-primary) 12%, transparent);
  border: 1px solid color-mix(in oklch, var(--color-primary) 30%, transparent);
  border-radius: 999px;
  white-space: nowrap;
}
</style>
