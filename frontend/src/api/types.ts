import type { GeoJSON } from "geojson";

/**
 * A single data point from a GPS track.
 * Used in SpeedBarGraph, the journey deck, and TrackGeoJsonData.
 */
export interface TrackDataPoint {
  lat: number;
  lng: number;
  speed: number;
  timestamp?: string;
  altitude?: number;
  course?: number;
  satellites?: number;
  hdop?: number;
  gps_date?: string;
  gps_time?: string;
  heel?: number | null;
  trim?: number | null;
  heading?: number | null;
}

/**
 * Unified track data structure with GeoJSON representation
 * Used by both device and web APIs
 */
export interface TrackGeoJsonData {
  markers: GeoJSON;
  data: TrackDataPoint[];
}

/**
 * Track selection from TrackSelector component
 */
export interface TrackSelection {
  source: "device" | "web" | "local";
  id: string | number;
  name: string;
}

/**
 * Travel mode for map speed color scales
 */
export type TravelMode = "sailing" | "walking" | "biking" | "driving";
