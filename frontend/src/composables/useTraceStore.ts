import { computed, ref } from "vue";
import type { LineString, Position } from "geojson";

export function useTraceStore() {
  const coordinates = ref<Position[]>([]);

  const isValid = computed(() => coordinates.value.length >= 2);

  function addPoint(lngLat: Position) {
    coordinates.value.push([lngLat[0], lngLat[1]]);
  }

  function undo() {
    coordinates.value.pop();
  }

  function clear() {
    coordinates.value = [];
  }

  function toLineString(): LineString | null {
    if (!isValid.value) return null;
    return {
      type: "LineString",
      coordinates: coordinates.value.map(([x, y]) => [x, y]),
    };
  }

  return {
    coordinates,
    isValid,
    addPoint,
    undo,
    clear,
    toLineString,
  };
}
