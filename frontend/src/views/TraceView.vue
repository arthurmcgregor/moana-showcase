<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef, watch } from "vue";
import { useRouter } from "vue-router";
import { useMutation, useQueryCache } from "@pinia/colada";
import { GeolocateControl, Map as MapboxMap, NavigationControl, type Map } from "mapbox-gl";
import MapboxDraw from "@mapbox/mapbox-gl-draw";
import "mapbox-gl/dist/mapbox-gl.css";
import "@mapbox/mapbox-gl-draw/dist/mapbox-gl-draw.css";
import { ArrowUturnLeftIcon, CheckIcon, XMarkIcon } from "@heroicons/vue/24/outline";
import { useTraceStore } from "@/composables/useTraceStore";
import { useVesselsQuery } from "@/composables/useQueries";
import { createJourney, type Journey, type Vessel } from "@/api/web";
import JourneyCreateModal from "@/components/JourneyCreateModal.vue";

const MAPBOX_ACCESS_TOKEN = import.meta.env.VITE_MAPBOX_ACCESS_TOKEN as string;

const router = useRouter();
const queryCache = useQueryCache();
const { state: vesselsState } = useVesselsQuery();

const trace = useTraceStore();
const mapContainer = ref<HTMLDivElement | null>(null);
const map = shallowRef<Map | null>(null);
const draw = shallowRef<MapboxDraw | null>(null);

const showModal = ref(false);
const selectedVessel = ref<number | null>(null);

onMounted(() => {
  if (!mapContainer.value) return;
  const m = new MapboxMap({
    accessToken: MAPBOX_ACCESS_TOKEN,
    container: mapContainer.value,
    style: "mapbox://styles/mapbox/streets-v12",
    center: [172.72, -43.61],
    zoom: 12,
  });
  map.value = m;

  m.addControl(new NavigationControl({ visualizePitch: false }), "bottom-right");
  m.addControl(
    new GeolocateControl({
      positionOptions: { enableHighAccuracy: true, timeout: 6000 },
      trackUserLocation: true,
    }),
    "bottom-right",
  );

  const d = new MapboxDraw({
    displayControlsDefault: false,
    defaultMode: "draw_line_string",
  });
  draw.value = d;
  m.addControl(d);

  const syncFromDraw = () => {
    const features = d.getAll().features;
    const line = features.find((f) => f.geometry.type === "LineString");
    if (!line || line.geometry.type !== "LineString") {
      trace.clear();
      return;
    }
    trace.coordinates.value = line.geometry.coordinates.map(
      ([x, y]) => [x, y] as [number, number],
    );
  };

  m.on("draw.create", syncFromDraw);
  m.on("draw.update", syncFromDraw);
  m.on("draw.delete", syncFromDraw);

  window.addEventListener("keydown", onKeydown);
  window.addEventListener("beforeunload", onBeforeUnload);
});

onBeforeUnmount(() => {
  window.removeEventListener("keydown", onKeydown);
  window.removeEventListener("beforeunload", onBeforeUnload);
  map.value?.remove();
});

function onKeydown(e: KeyboardEvent) {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "z") {
    e.preventDefault();
    undoLastPoint();
  }
}

function onBeforeUnload(e: BeforeUnloadEvent) {
  if (trace.isValid.value && !showModal.value) {
    e.preventDefault();
  }
}

function undoLastPoint() {
  const d = draw.value;
  if (!d) return;
  const features = d.getAll().features;
  const line = features.find((f) => f.geometry.type === "LineString");
  if (!line || line.geometry.type !== "LineString") return;
  const coords = line.geometry.coordinates.slice(0, -1);
  if (coords.length === 0) {
    d.deleteAll();
    trace.clear();
  } else {
    d.delete(String(line.id));
    d.add({
      id: String(line.id),
      type: "Feature",
      properties: {},
      geometry: { type: "LineString", coordinates: coords },
    });
    d.changeMode("draw_line_string", { featureId: String(line.id), from: coords[coords.length - 1] });
    trace.coordinates.value = coords.map(([x, y]) => [x, y] as [number, number]);
  }
}

function onDone() {
  if (!trace.isValid.value) return;
  showModal.value = true;
}

function onDiscard() {
  draw.value?.deleteAll();
  trace.clear();
  router.push({ name: "journeys" });
}

watch(
  () => vesselsState.value,
  (state) => {
    if (state.status === "success" && state.data.length > 0 && selectedVessel.value === null) {
      selectedVessel.value = state.data[0].id;
    }
  },
  { immediate: true },
);

const { mutate: submitCreate, isLoading: submitting, error: submitError } = useMutation({
  mutation: (payload: { name: string; description: string; notes: string; startedAt: string; endedAt: string }) => {
    const manualRoute = trace.toLineString();
    if (!manualRoute || selectedVessel.value === null) {
      throw new Error("Vessel and route are required");
    }
    const body: Partial<Journey> = {
      vessel: selectedVessel.value,
      name: payload.name,
      description: payload.description,
      notes: payload.notes,
      started_at: payload.startedAt,
      ended_at: payload.endedAt,
      manual_route: manualRoute,
    };
    return createJourney(body);
  },
  onSuccess: (journey) => {
    queryCache.invalidateQueries({ key: ["journeys"] });
    router.push({ name: "journey-detail", params: { id: journey.id } });
  },
});

function onModalCancel() {
  showModal.value = false;
}
</script>

<template>
  <div class="trace-view">
    <div
      ref="mapContainer"
      class="trace-map"
    />

    <div class="trace-toolbar">
      <div class="trace-indicator">
        <span class="dot" />
        <span v-if="!trace.isValid.value">
          Tap map to add point ({{ trace.coordinates.value.length }}/2 minimum)
        </span>
        <span v-else>
          {{ trace.coordinates.value.length }} points placed
        </span>
      </div>

      <div class="trace-actions">
        <AppSelect
          v-if="vesselsState.status === 'success'"
          v-model="selectedVessel"
          class="vessel-select"
        >
          <option
            v-for="v in (vesselsState.data as Vessel[])"
            :key="v.id"
            :value="v.id"
          >
            {{ v.name }}
          </option>
        </AppSelect>
        <button
          type="button"
          class="btn btn-sm"
          :disabled="trace.coordinates.value.length === 0"
          @click="undoLastPoint"
        >
          <ArrowUturnLeftIcon class="size-4" />
          Undo
        </button>
        <button
          type="button"
          class="btn btn-sm"
          @click="onDiscard"
        >
          <XMarkIcon class="size-4" />
          Discard
        </button>
        <button
          type="button"
          class="btn btn-sm btn-primary"
          :disabled="!trace.isValid.value"
          @click="onDone"
        >
          <CheckIcon class="size-4" />
          Done
        </button>
      </div>
    </div>

    <JourneyCreateModal
      :show="showModal"
      mode="manual"
      :submitting="submitting"
      :error="submitError"
      @cancel="onModalCancel"
      @discard="onDiscard"
      @create-manual-journey="submitCreate"
    />
  </div>
</template>

<style scoped>
.trace-view {
  position: relative;
  height: calc(100vh - 64px);
  width: 100%;
}

.trace-map {
  position: absolute;
  inset: 0;
}

.trace-toolbar {
  position: absolute;
  left: 12px;
  right: 12px;
  top: 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 14px;
  background: var(--color-base-100);
  border: 1px solid var(--color-base-300);
  border-radius: 10px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
  z-index: 10;
  flex-wrap: wrap;
}

.trace-indicator {
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: ui-monospace, monospace;
  font-size: 12px;
  color: var(--color-base-content);
}

.trace-indicator .dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--color-accent, #14b8a6);
  animation: pulse 1.6s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.4; }
}

.trace-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.vessel-select {
  min-width: 140px;
}
</style>
