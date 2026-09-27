<script setup lang="ts">
import { ref } from "vue";
import { useMutation, useQueryCache } from "@pinia/colada";
import { CloudArrowUpIcon } from "@heroicons/vue/24/outline";
import { useAuthStore } from "@/stores";
import { uploadWeatherCsv } from "@/api/web";
import { useWeatherStationsQuery } from "@/composables/useQueries";

const authStore = useAuthStore();
const queryCache = useQueryCache();

const selectedStationId = ref<number | null>(null);
const selectedFile = ref<File | null>(null);

const { state: stationsState } = useWeatherStationsQuery();

const { mutate, state, isLoading: uploading } = useMutation({
  mutation: ({ stationId, file }: { stationId: number; file: File }) =>
    uploadWeatherCsv(stationId, file),
  onSuccess: () => {
    selectedFile.value = null;
    queryCache.invalidateQueries({ key: ["journeyTracks"] });
    queryCache.invalidateQueries({ key: ["journeyWeatherSummary"] });
  },
});

function onUpload() {
  const stationId = selectedStationId.value;
  const file = selectedFile.value;
  if (stationId == null || file == null) return;
  mutate({ stationId, file });
}

function formatDate(iso: string) {
  return new Date(iso).toLocaleString();
}
</script>

<template>
  <div class="mx-auto w-full max-w-xl">
    <PageHeader
      title="Upload Weather Data"
      subtitle="Import weather observations from a CSV file into a station."
    />

    <AppNotice
      v-if="authStore.isLoaded && !authStore.isAdmin"
      variant="info"
    >
      You have view-only access. Weather uploads require admin permissions.
    </AppNotice>

    <form
      v-else
      class="panel"
      @submit.prevent="onUpload"
    >
      <AppField label="Weather Station">
        <AppSelect
          v-model="selectedStationId"
          :disabled="stationsState.status === 'pending'"
        >
          <option
            :value="null"
            disabled
          >
            Select a station…
          </option>
          <option
            v-for="station in stationsState.data ?? []"
            :key="station.id"
            :value="station.id"
          >
            {{ station.name }}
          </option>
        </AppSelect>
      </AppField>

      <AppField label="CSV File">
        <AppDropzone
          v-if="!selectedFile"
          accept=".csv,text/csv"
          :icon="CloudArrowUpIcon"
          primary="Drop a weather CSV here"
          @select="(f: File) => (selectedFile = f)"
        />
        <AppFileChip
          v-else
          :file="selectedFile"
          @remove="selectedFile = null"
        />
      </AppField>

      <AppNotice
        v-if="state.status === 'success'"
        variant="success"
      >
        <p class="font-semibold">
          Imported {{ state.data.observations_created }} observation{{ state.data.observations_created === 1 ? "" : "s" }}
        </p>
        <p class="notice__sub">
          {{ formatDate(state.data.time_range.start) }} — {{ formatDate(state.data.time_range.end) }}
        </p>
      </AppNotice>

      <AppNotice
        v-if="state.status === 'success' && state.data.warnings?.length"
        variant="warning"
      >
        <ul class="space-y-1 text-sm">
          <li
            v-for="(warning, i) in state.data.warnings"
            :key="i"
          >
            {{ warning }}
          </li>
        </ul>
      </AppNotice>

      <AppNotice
        v-if="state.status === 'error'"
        variant="error"
      >
        {{ (state.error as Error).message }}
      </AppNotice>

      <div class="actions">
        <BtnSave
          :loading="uploading"
          :disabled="!selectedStationId || !selectedFile"
        >
          {{ uploading ? "Uploading…" : "Upload Weather" }}
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

.notice__sub {
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 400;
  letter-spacing: 0.03em;
  color: color-mix(in oklch, var(--color-base-content) 55%, transparent);
  margin-top: 2px;
}

.actions {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
  padding-top: 4px;
}
</style>
