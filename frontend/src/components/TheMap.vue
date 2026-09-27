<script setup lang="ts">
import { computed } from "vue";
import {
  MapboxMap,
  MapboxLayer,
  MapboxSource,
  MapboxNavigationControl,
  MapboxGeolocateControl,
} from "@studiometa/vue-mapbox-gl";
import type { Map as MapboxMapInstance, MapLayerMouseEvent } from "mapbox-gl";
import type { FeatureCollection, Feature, LineString } from "geojson";
import type { Annotation, Mode, JourneyPhoto } from "@/api/web";
import { useMapView } from "@/composables/useMapView";
import { chaikin } from "@/utils/smoothLine";

const MAPBOX_ACCESS_TOKEN = import.meta.env.VITE_MAPBOX_ACCESS_TOKEN as string;

const props = defineProps<{
  markers: FeatureCollection;
  travelMode?: string;
  cursorPosition?: [number, number] | null;
  annotations?: Annotation[];
  modes?: Mode[];
  photos?: JourneyPhoto[];
  scopeWindow?: { startMs: number; endMs: number } | null;
  playheadMs?: number | null;
  hasPlayhead?: boolean;
  isPlaying?: boolean;
  /** The scope window is tracking the boat — dim to it, but hold the camera. */
  following?: boolean;
}>();

const emit = defineEmits<{
  mapClick: [lngLat: [number, number]];
  photoClick: [assetId: string];
  photoCluster: [assetIds: string[]];
}>();

const PHOTO_SOURCE = "photo-cluster";

const photosSource = computed(() => ({
  type: "geojson" as const,
  cluster: true,
  clusterRadius: 50,
  clusterMaxZoom: 14,
  data: {
    type: "FeatureCollection" as const,
    features: (props.photos ?? [])
      .filter((p): p is JourneyPhoto & { lat: number; lng: number } =>
        typeof p.lat === "number" && typeof p.lng === "number",
      )
      .map((p) => ({
        type: "Feature" as const,
        geometry: { type: "Point" as const, coordinates: [p.lng, p.lat] },
        properties: { id: p.id, takenAt: p.takenAt, type: p.type },
      })),
  },
}));

const photoClustersLayer = Object.freeze({
  type: "circle",
  source: PHOTO_SOURCE,
  filter: ["has", "point_count"],
  paint: {
    "circle-color": "#14b8a6",
    "circle-radius": ["step", ["get", "point_count"], 12, 10, 15, 50, 18],
    "circle-stroke-color": "#ffffff",
    "circle-stroke-width": 2,
    "circle-opacity": 0.95,
  },
});
const photoClusterGlyphLayer = Object.freeze({
  type: "symbol",
  source: PHOTO_SOURCE,
  filter: ["has", "point_count"],
  layout: {
    "icon-image": "camera-glyph",
    "icon-size": 1.1,
    "icon-allow-overlap": true,
  },
});
const photoUnclusteredLayer = Object.freeze({
  type: "symbol",
  source: PHOTO_SOURCE,
  filter: ["!", ["has", "point_count"]],
  layout: {
    "icon-image": "camera-icon",
    "icon-size": 1.1,
    "icon-allow-overlap": true,
  },
});

const { onMapCreated: mapViewOnCreated, initialBounds, layers } = useMapView(props, emit);

function onMapCreated(map: MapboxMapInstance) {
  mapViewOnCreated(map);

  const setCursor = (style: "pointer" | "") => { map.getCanvas().style.cursor = style; };

  map.on("click", "photo-unclustered", (e: MapLayerMouseEvent) => {
    const id = e.features?.[0]?.properties?.id;
    if (typeof id === "string") emit("photoClick", id);
  });
  map.on("mouseenter", "photo-unclustered", () => setCursor("pointer"));
  map.on("mouseleave", "photo-unclustered", () => setCursor(""));

  map.on("click", "photo-clusters", (e: MapLayerMouseEvent) => {
    const feature = e.features?.[0];
    const clusterId = feature?.properties?.cluster_id;
    const count = feature?.properties?.point_count ?? 0;
    if (clusterId == null || count === 0) return;
    const source = map.getSource(PHOTO_SOURCE);
    if (!source || source.type !== "geojson") return;
    source.getClusterLeaves(clusterId, count, 0, (err, leaves) => {
      if (err || !leaves) return;
      const ids = leaves
        .map((f) => f.properties?.id)
        .filter((id): id is string => typeof id === "string");
      if (ids.length) emit("photoCluster", ids);
    });
  });
  map.on("mouseenter", "photo-clusters", () => setCursor("pointer"));
  map.on("mouseleave", "photo-clusters", () => setCursor(""));
}

const manualRouteData = computed<FeatureCollection>(() => ({
  type: "FeatureCollection",
  features: props.markers.features
    .filter((f): f is Feature<LineString> => f.geometry.type === "LineString")
    .map((f) => ({
      ...f,
      geometry: {
        ...f.geometry,
        coordinates: chaikin(f.geometry.coordinates as [number, number][]),
      },
    })),
}));

const manualRouteLayer = computed(() => ({
  type: "line",
  source: { type: "geojson", data: manualRouteData.value },
  paint: {
    "line-color": "#8c7cb8",
    "line-width": 4,
    "line-opacity": 0.9,
  },
  layout: { "line-cap": "round", "line-join": "round" },
}));
</script>

<template>
  <MapboxMap
    class="h-full w-full"
    :access-token="MAPBOX_ACCESS_TOKEN"
    map-style="mapbox://styles/mapbox/streets-v12"
    :bounds="initialBounds"
    :fit-bounds-options="{ padding: { top: 50, bottom: 50, left: 50, right: 50 } }"
    :max-zoom="20"
    :min-zoom="0"
    logo-position="bottom-right"
    @mb-created="onMapCreated"
  >
    <MapboxLayer
      id="manual-route"
      :options="manualRouteLayer"
    />
    <MapboxLayer
      id="points"
      :options="layers.points"
    />
    <MapboxLayer
      id="cursor"
      :options="layers.cursor"
    />
    <MapboxLayer
      id="annotations"
      :options="layers.annotations"
    />
    <MapboxLayer
      id="modes"
      :options="layers.modes"
    />
    <MapboxSource
      :id="PHOTO_SOURCE"
      :options="photosSource"
    />
    <MapboxLayer
      id="photo-clusters"
      :options="photoClustersLayer"
    />
    <MapboxLayer
      id="photo-cluster-glyph"
      :options="photoClusterGlyphLayer"
    />
    <MapboxLayer
      id="photo-unclustered"
      :options="photoUnclusteredLayer"
    />
    <!-- Every control on the left edge: the journey screen's readout sits top
         right, over water the controls would otherwise be stacked on. -->
    <MapboxNavigationControl position="bottom-left" />
    <MapboxGeolocateControl
      position="top-left"
      :track-user-location="true"
      :position-options="{ enableHighAccuracy: true, timeout: 6000 }"
    />
  </MapboxMap>
</template>
