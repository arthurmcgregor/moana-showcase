import { computed } from "vue";
import type { Vessel } from "@/api/web";
import { useVesselsQuery } from "@/composables/useQueries";

export function useVesselName() {
  const { state } = useVesselsQuery();

  const byId = computed(() => {
    if (state.value.status !== "success") return null;
    return new Map(state.value.data.map((v: Vessel) => [v.id, v.name]));
  });

  return (id: number | null): string => {
    if (id === null) return "Unknown";
    const map = byId.value;
    if (!map) return "Loading…";
    return map.get(id) ?? "Unknown";
  };
}
