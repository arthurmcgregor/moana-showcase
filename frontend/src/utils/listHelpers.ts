import type { Journey, Track } from "@/api/web";
import { formatDuration, formatAbsoluteDate } from "@/utils/formatters";

export type MetaItem = { label: string; value: string };
export type VesselNameLookup = (id: number | null) => string;

export function formatJourneyDistance(journey: Journey): string {
  if (journey.distance_nm != null && journey.distance_nm > 0) {
    return `${journey.distance_nm.toFixed(1)} nm`;
  }
  if (journey.estimated_distance_nm != null) {
    return `${journey.estimated_distance_nm.toFixed(1)} nm (est.)`;
  }
  return "—";
}

export function isTraceOnlyJourney(journey: Journey): boolean {
  return journey.manual_route != null && (journey.distance_nm ?? 0) <= 0;
}

export function formatPointsCount(count: number): string {
  if (count >= 1000) {
    return `${(count / 1000).toFixed(1)}k`;
  }
  return count.toString();
}

export function trackMeta(track: Track, vesselName: VesselNameLookup): MetaItem[] {
  return [
    { label: "Vessel", value: vesselName(track.vessel) },
    { label: "Departed", value: formatAbsoluteDate(track.started_at) },
    { label: "Duration", value: formatDuration(track.started_at, track.ended_at) },
    { label: "Points", value: formatPointsCount(track.points_count) },
  ];
}

export function journeyMeta(journey: Journey, vesselName: VesselNameLookup): MetaItem[] {
  const traceOnly = isTraceOnlyJourney(journey);
  const items: MetaItem[] = [
    { label: "Vessel", value: vesselName(journey.vessel) },
    { label: "Departed", value: formatAbsoluteDate(journey.started_at) },
  ];
  if (!traceOnly) {
    items.push({ label: "Duration", value: formatDuration(journey.started_at, journey.ended_at) });
  }
  items.push({ label: "Distance", value: formatJourneyDistance(journey) });
  if (!traceOnly) {
    items.push({
      label: "Legs",
      value: journey.legs_count
        ? `${journey.legs_count} leg${journey.legs_count !== 1 ? "s" : ""}`
        : "—",
    });
  }
  return items;
}
