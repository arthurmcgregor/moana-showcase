import { parse, unparse } from "papaparse";

import { config } from "../config";
import type { GeoJSON } from "geojson";
import type { TrackGeoJsonData } from "./types";

function getCsrfToken(): string | undefined {
  return document.cookie
    .split("; ")
    .find((row) => row.startsWith("csrftoken="))
    ?.split("=")[1];
}

function apiFetch(url: string | URL, init?: RequestInit): Promise<Response> {
  const method = init?.method?.toUpperCase();
  const headers = new Headers(init?.headers);
  if (method && ["POST", "PATCH", "PUT", "DELETE"].includes(method)) {
    const csrf = getCsrfToken();
    if (csrf) headers.set("X-CSRFToken", csrf);
  }
  return fetch(url, { credentials: "include", ...init, headers });
}

// ============================================================================
// Types
// ============================================================================

export interface Vessel {
  id: number;
  name: string;
  description: string;
  vessel_type: string;
  created_at: string;
}

export interface Person {
  id: number;
  first_name: string;
  last_name: string;
  email: string;
  notes: string;
  created_at: string;
}

export interface PersonHistory {
  person: Person;
  aboard_records: AboardRecord[];
}

export interface Track {
  id: number;
  vessel: number | null;
  vessel_name?: string;
  name: string;
  description: string;
  started_at: string;
  ended_at: string | null;
  imported_at: string;
  points_count: number;
  source_filename: string;
}

export interface TrackPoint {
  id: number;
  track: number;
  location: {
    type: "Point";
    coordinates: [number, number]; // [lng, lat]
  };
  speed: number | null;
  satellites: number | null;
  altitude: number | null;
  hdop: number | null;
  course: number | null;
  heel: number | null;
  trim: number | null;
  heading: number | null;
  timestamp: string;
}

export type ModeType =
  | "SAIL"
  | "MOTORSAIL"
  | "MOTOR"
  | "ROW"
  | "UNPOWERED"
  | "ANCHOR"
  | "ALONGSIDE"
  | "BEACHED"
  | "TRAILERED";

export interface Mode {
  id: number;
  vessel: number;
  vessel_name?: string;
  mode: ModeType;
  started_at: string;
  ended_at?: string; // Computed field
  metadata: Record<string, unknown>;
  notes: string;
}

export type AnnotationType = "TACK" | "GYBE" | "COURSE_CHANGE" | "NOTE";

export interface Annotation {
  id: number;
  vessel: number;
  vessel_name?: string;
  type: AnnotationType;
  timestamp: string;
  metadata: Record<string, unknown>;
  notes: string;
}

export interface AboardRecord {
  id: number;
  vessel: number;
  vessel_name?: string;
  person: number;
  person_name?: string;
  started_at: string;
  ended_at: string | null;
  notes: string;
}

export interface ModeStatistics {
  distance_nm: number;
  duration_seconds: number;
  max_speed_knots: number;
  avg_speed_knots: number;
}

export interface LegStatistics {
  total_distance_nm: number;
  total_duration_seconds: number;
  max_speed_knots: number;
  avg_speed_knots: number;
  by_mode: Record<string, ModeStatistics>;
}

export interface Leg {
  id: number;
  journey: number;
  name: string;
  started_at: string;
  ended_at: string | null;
  notes: string;
  duration: number | null;
  statistics?: LegStatistics;
}

export interface Journey {
  id: number;
  vessel: number;
  vessel_name?: string;
  name: string;
  started_at: string;
  ended_at: string | null;
  description: string;
  notes: string;
  weather_station: number | null;
  duration: number | null;
  legs_count?: number;
  distance_nm?: number;
  estimated_distance_nm?: number | null;
  manual_route?: GeoJSON.LineString | null;
  legs?: Leg[];
  statistics?: LegStatistics;
}

export interface JourneyFull {
  journey: Journey;
  modes: Mode[];
  annotations: Annotation[];
  aboard: AboardRecord[];
}

export interface GeoJSONPoint {
  type: "Point";
  coordinates: [number, number]; // [longitude, latitude]
}

export interface GeoJSONFeature {
  type: "Feature";
  geometry: GeoJSONPoint;
  properties: {
    id?: number;
    timestamp: string;
    speed?: number;
    altitude?: number;
    course?: number;
    heel?: number | null;
    trim?: number | null;
    heading?: number | null;
    track_id: number;
    wind_direction?: number | null;
    wind_speed?: number | null;
    gust_speed?: number | null;
  };
}

export interface WeatherStation {
  id: number;
  name: string;
  description: string;
  location: unknown;
  created_at: string;
}

export interface WeatherSummarySeriesPoint {
  t: string;
  wind_speed: number | null;
  gust: number | null;
  wind_direction: number | null;
  temperature: number | null;
  humidity: number | null;
  rain: number | null;
  tide: number | null;
}

export interface WeatherSummaryBin {
  direction_deg: number;
  label: string;
  bands: number[];
  total: number;
}

export interface WeatherSummarySpeedBand {
  label: string;
  min: number;
  max: number | null;
}

export interface WeatherSummary {
  station: { id: number; name: string } | null;
  window: { started_at: string | null; ended_at: string | null; observation_count: number };
  wind: { min: number | null; max: number | null; avg: number | null };
  gust: { max: number | null; avg: number | null };
  temperature: { min: number | null; max: number | null; avg: number | null };
  humidity: { min: number | null; max: number | null; avg: number | null };
  rain_total_mm: number;
  tide: {
    first: number | null;
    last: number | null;
    delta: number | null;
    min: number | null;
    max: number | null;
  };
  wind_rose: WeatherSummaryBin[];
  wind_rose_speed_bands: WeatherSummarySpeedBand[];
  series: WeatherSummarySeriesPoint[];
}

export interface GeoJSONFeatureCollection {
  type: "FeatureCollection";
  features: GeoJSONFeature[];
}

export interface PaginatedResponse<T> {
  count: number;
  next: string | null;
  previous: string | null;
  results: T[];
}

// ============================================================================
// Auth API
// ============================================================================

export interface UserInfo {
  username: string;
  is_admin: boolean;
}

export async function fetchMe(): Promise<UserInfo> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/me/`);
  if (!response.ok) throw new Error("Failed to fetch user info");
  return await response.json();
}

// ============================================================================
// Vessels API
// ============================================================================

export async function fetchVessels(): Promise<PaginatedResponse<Vessel>> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/vessels/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch vessels: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchVessel(id: number): Promise<Vessel> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/vessels/${id}/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch vessel ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function createVessel(vessel: Partial<Vessel>): Promise<Vessel> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/vessels/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(vessel),
  });
  if (!response.ok) {
    throw new Error(`Failed to create vessel: ${response.statusText}`);
  }
  return await response.json();
}

export async function updateVessel(id: number, vessel: Partial<Vessel>): Promise<Vessel> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/vessels/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(vessel),
  });
  if (!response.ok) {
    throw new Error(`Failed to update vessel ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteVessel(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/vessels/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete vessel ${id}: ${response.statusText}`);
  }
}

// ============================================================================
// People API
// ============================================================================

export async function fetchPeople(): Promise<PaginatedResponse<Person>> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/people/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch people: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchPerson(id: number): Promise<Person> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/people/${id}/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch person ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchPersonHistory(id: number): Promise<PersonHistory> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/people/${id}/history/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch person history ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function createPerson(person: Partial<Person>): Promise<Person> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/people/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(person),
  });
  if (!response.ok) {
    throw new Error(`Failed to create person: ${response.statusText}`);
  }
  return await response.json();
}

export async function updatePerson(id: number, person: Partial<Person>): Promise<Person> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/people/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(person),
  });
  if (!response.ok) {
    throw new Error(`Failed to update person ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deletePerson(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/people/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete person ${id}: ${response.statusText}`);
  }
}

// ============================================================================
// Tracks API
// ============================================================================

export async function fetchTracks(params?: {
  vessel?: number;
  ordering?: string;
}): Promise<PaginatedResponse<Track>> {
  const url = new URL(`${config.webServerBaseUrl}/api/tracks/tracks/`);
  if (params?.vessel) url.searchParams.set("vessel", params.vessel.toString());
  if (params?.ordering) url.searchParams.set("ordering", params.ordering);

  const response = await apiFetch(url.toString());
  if (!response.ok) {
    throw new Error(`Failed to fetch tracks: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchTrack(id: number): Promise<Track> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/tracks/${id}/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch track ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchTrackPoints(id: number): Promise<PaginatedResponse<TrackPoint>> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/tracks/${id}/points/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch track points ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export interface UploadTrackCsvResponse {
  success: boolean;
  track_id: number;
  track_name: string;
  vessel_id: number;
  vessel_name: string;
  points_created: number;
  started_at: string;
  ended_at: string;
  /** Present only when an events sidecar was sent (issue #139). */
  modes_created?: number;
  /**
   * Rows the server kept the upload for but could not use: a CSV row it could
   * not parse, a sidecar line that was malformed or timestamped outside the
   * track, a mis-press burst it collapsed. The upload still succeeded, so
   * nothing else will ever mention these — show them (issue #156).
   */
  warnings?: string[];
}

export async function uploadTrackCsv(
  file: File,
  metadata: {
    name: string;
    vessel: number;
    started_at?: string;
  },
  events?: File,
): Promise<UploadTrackCsvResponse> {
  const formData = new FormData();
  formData.append("file", file);
  formData.append("name", metadata.name);
  formData.append("vessel", metadata.vessel.toString());
  if (metadata.started_at) {
    formData.append("started_at", metadata.started_at);
  }
  if (events) {
    // Optional watch-events sidecar (issue #148); the server imports it idempotently.
    formData.append("events", events, events.name);
  }

  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/tracks/upload-csv/`, {
    method: "POST",
    body: formData,
  });

  if (!response.ok) {
    const detail = await response.text();
    throw new Error(detail || `Failed to upload track CSV: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteTrack(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/tracks/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete track ${id}: ${response.statusText}`);
  }
}

export async function deleteTrackPoint(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/track-points/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete track point ${id}: ${response.statusText}`);
  }
}

/**
 * Fetches a track's points as GeoJSON from the dedicated endpoint
 * @param id The track ID
 */
export async function fetchWebTrackGeoJson(id: number): Promise<TrackGeoJsonData> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/tracks/${id}/geojson/`);

  if (!response.ok) {
    throw new Error(`Failed to fetch track GeoJSON ${id}: ${response.statusText}`);
  }

  const geoJson = await response.json();

  if (!geoJson || geoJson.type !== "FeatureCollection") {
    throw new Error("Invalid GeoJSON response format");
  }

  const features = geoJson.features || [];

  if (features.length === 0) {
    // Return empty GeoJSON structures if no points
    return {
      markers: { type: "FeatureCollection", features: [] },
      data: [],
    };
  }

  const markers: GeoJSON = geoJson;

  const data = features.map(
    (feature: {
      geometry: { coordinates: [number, number] };
      properties?: {
        speed?: number;
        timestamp?: string;
        altitude?: number;
        course?: number;
        satellites?: number;
        hdop?: number;
        heel?: number | null;
        trim?: number | null;
        heading?: number | null;
      };
    }) => ({
      lat: feature.geometry.coordinates[1],
      lng: feature.geometry.coordinates[0],
      speed: feature.properties?.speed || 0,
      timestamp: feature.properties?.timestamp,
      altitude: feature.properties?.altitude,
      course: feature.properties?.course,
      satellites: feature.properties?.satellites,
      hdop: feature.properties?.hdop,
      heel: feature.properties?.heel,
      trim: feature.properties?.trim,
      heading: feature.properties?.heading,
    }),
  );

  return { markers, data };
}

// ============================================================================
// Modes API
// ============================================================================

export async function fetchModes(params?: {
  vessel?: number;
  ordering?: string;
}): Promise<PaginatedResponse<Mode>> {
  const url = new URL(`${config.webServerBaseUrl}/api/tracks/modes/`);
  if (params?.vessel) url.searchParams.set("vessel", params.vessel.toString());
  if (params?.ordering) url.searchParams.set("ordering", params.ordering);

  const response = await apiFetch(url.toString());
  if (!response.ok) {
    throw new Error(`Failed to fetch modes: ${response.statusText}`);
  }
  return await response.json();
}

export async function createMode(mode: Partial<Mode>): Promise<Mode> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/modes/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(mode),
  });
  if (!response.ok) {
    throw new Error(`Failed to create mode: ${response.statusText}`);
  }
  return await response.json();
}

export async function updateMode(id: number, mode: Partial<Mode>): Promise<Mode> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/modes/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(mode),
  });
  if (!response.ok) {
    throw new Error(`Failed to update mode ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteMode(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/modes/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete mode ${id}: ${response.statusText}`);
  }
}

// ============================================================================
// Annotations API
// ============================================================================

export async function fetchAnnotations(params?: {
  vessel?: number;
  ordering?: string;
}): Promise<PaginatedResponse<Annotation>> {
  const url = new URL(`${config.webServerBaseUrl}/api/tracks/annotations/`);
  if (params?.vessel) url.searchParams.set("vessel", params.vessel.toString());
  if (params?.ordering) url.searchParams.set("ordering", params.ordering);

  const response = await apiFetch(url.toString());
  if (!response.ok) {
    throw new Error(`Failed to fetch annotations: ${response.statusText}`);
  }
  return await response.json();
}

export async function createAnnotation(annotation: Partial<Annotation>): Promise<Annotation> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/annotations/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(annotation),
  });
  if (!response.ok) {
    throw new Error(`Failed to create annotation: ${response.statusText}`);
  }
  return await response.json();
}

export async function updateAnnotation(
  id: number,
  annotation: Partial<Annotation>,
): Promise<Annotation> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/annotations/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(annotation),
  });
  if (!response.ok) {
    throw new Error(`Failed to update annotation ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteAnnotation(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/annotations/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete annotation ${id}: ${response.statusText}`);
  }
}

// ============================================================================
// Aboard API
// ============================================================================

export async function fetchAboardRecords(params?: {
  vessel?: number;
  person?: number;
  current?: boolean;
  ordering?: string;
}): Promise<PaginatedResponse<AboardRecord>> {
  const url = new URL(`${config.webServerBaseUrl}/api/tracks/aboard/`);
  if (params?.vessel) url.searchParams.set("vessel", params.vessel.toString());
  if (params?.person) url.searchParams.set("person", params.person.toString());
  if (params?.current !== undefined) url.searchParams.set("current", params.current.toString());
  if (params?.ordering) url.searchParams.set("ordering", params.ordering);

  const response = await apiFetch(url.toString());
  if (!response.ok) {
    throw new Error(`Failed to fetch aboard records: ${response.statusText}`);
  }
  return await response.json();
}

export async function createAboardRecord(record: Partial<AboardRecord>): Promise<AboardRecord> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/aboard/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(record),
  });
  if (!response.ok) {
    throw new Error(`Failed to create aboard record: ${response.statusText}`);
  }
  return await response.json();
}

export async function updateAboardRecord(
  id: number,
  record: Partial<AboardRecord>,
): Promise<AboardRecord> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/aboard/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(record),
  });
  if (!response.ok) {
    throw new Error(`Failed to update aboard record ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteAboardRecord(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/aboard/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete aboard record ${id}: ${response.statusText}`);
  }
}

// ============================================================================
// Journeys API
// ============================================================================

export async function fetchJourneys(params?: {
  vessel?: number;
  ordering?: string;
}): Promise<PaginatedResponse<Journey>> {
  const url = new URL(`${config.webServerBaseUrl}/api/tracks/journeys/`);
  if (params?.vessel) url.searchParams.set("vessel", params.vessel.toString());
  if (params?.ordering) url.searchParams.set("ordering", params.ordering);

  const response = await apiFetch(url.toString());
  if (!response.ok) {
    throw new Error(`Failed to fetch journeys: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchJourney(id: number | string): Promise<Journey> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/${id}/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch journey ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchJourneyModes(id: number): Promise<Mode[]> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/${id}/modes/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch journey modes ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchJourneyAnnotations(id: number): Promise<Annotation[]> {
  const response = await apiFetch(
    `${config.webServerBaseUrl}/api/tracks/journeys/${id}/annotations/`,
  );
  if (!response.ok) {
    throw new Error(`Failed to fetch journey annotations ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchJourneyAboard(id: number): Promise<AboardRecord[]> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/${id}/aboard/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch journey aboard ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchJourneyTracks(
  id: number,
): Promise<GeoJSONFeatureCollection> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/${id}/tracks/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch journey tracks ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchJourneyWeatherSummary(id: number): Promise<WeatherSummary> {
  const response = await apiFetch(
    `${config.webServerBaseUrl}/api/tracks/journeys/${id}/weather-summary/`,
  );
  if (!response.ok) {
    throw new Error(`Failed to fetch journey weather summary ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export interface JourneyPhoto {
  id: string;
  takenAt: string | null;
  type: "IMAGE" | "VIDEO";
  thumbUrl: string;
  previewUrl: string;
  immichUrl: string | null;
  lat?: number;
  lng?: number;
}

export interface JourneyPhotosResponse {
  photos: JourneyPhoto[];
  immichPublicUrl: string | null;
  window: { start: string; end: string } | null;
}

export async function fetchJourneyPhotos(id: number): Promise<JourneyPhotosResponse> {
  const response = await apiFetch(
    `${config.webServerBaseUrl}/api/tracks/journeys/${id}/photos/`,
  );
  if (!response.ok) {
    throw new Error(`Failed to fetch journey photos ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function fetchWeatherStations(): Promise<WeatherStation[]> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/weather-stations/`);
  if (!response.ok) {
    throw new Error(`Failed to fetch weather stations: ${response.statusText}`);
  }
  const data = await response.json();
  return data.results;
}

export interface UploadWeatherCsvResponse {
  success: boolean;
  station_id: number;
  observations_created: number;
  time_range: { start: string; end: string };
  warnings?: string[];
}

// Maps known header variants (from LPC exports and similar providers) onto the
// canonical names that the Django weather upload endpoint expects. Keys are
// matched case-insensitively after collapsing whitespace.
const WEATHER_HEADER_ALIASES: Record<string, string> = {
  "time": "Time",
  "temperature (°c)": "Temperature (°C)",
  "temperature (c)": "Temperature (°C)",
  "rain accumulation (mm)": "Rain Accumulation (mm)",
  "10m acc rain (mm)": "Rain Accumulation (mm)",
  "rain (mm)": "Rain Accumulation (mm)",
  "wind direction (°)": "Wind Direction (°)",
  "wind direction (deg)": "Wind Direction (°)",
  "wind speed (kn)": "Wind Speed (kn)",
  "gust speed (kn)": "Gust Speed (kn)",
  "humidity (%)": "Humidity (%)",
  "tide observation (m)": "Tide Observation (m)",
  "tide observations (m)": "Tide Observation (m)",
};

function normalizeHeader(raw: string): string {
  const key = raw.trim().replace(/\s+/g, " ").toLowerCase();
  return WEATHER_HEADER_ALIASES[key] ?? raw;
}

async function normalizeWeatherCsvFile(file: File): Promise<File> {
  const text = await file.text();
  const breakMatch = text.match(/\r\n|\n|\r/);
  const headerLine = breakMatch ? text.slice(0, breakMatch.index) : text;
  const rest = breakMatch ? text.slice(breakMatch.index) : "";

  const parsed = parse<string[]>(headerLine, { skipEmptyLines: false });
  const originalHeaders = parsed.data[0] ?? [];
  const normalizedHeaders = originalHeaders.map(normalizeHeader);

  if (normalizedHeaders.every((h, i) => h === originalHeaders[i])) return file;

  return new File([unparse([normalizedHeaders]) + rest], file.name, {
    type: file.type || "text/csv",
  });
}

export async function uploadWeatherCsv(
  stationId: number,
  file: File,
): Promise<UploadWeatherCsvResponse> {
  const normalizedFile = await normalizeWeatherCsvFile(file);
  const formData = new FormData();
  formData.append("file", normalizedFile);

  const response = await apiFetch(
    `${config.webServerBaseUrl}/api/tracks/weather-stations/${stationId}/upload-csv/`,
    { method: "POST", body: formData },
  );
  if (!response.ok) {
    const detail = await response.text();
    throw new Error(detail || `Upload failed: ${response.statusText}`);
  }
  return await response.json();
}

export async function createJourney(journey: Partial<Journey>): Promise<Journey> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(journey),
  });
  if (!response.ok) {
    throw new Error(`Failed to create journey: ${response.statusText}`);
  }
  return await response.json();
}

export async function updateJourney(id: number, journey: Partial<Journey>): Promise<Journey> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(journey),
  });
  if (!response.ok) {
    throw new Error(`Failed to update journey ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteJourney(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/journeys/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete journey ${id}: ${response.statusText}`);
  }
}

// ============================================================================
// Legs API
// ============================================================================

export async function fetchLegs(params?: {
  journey?: number;
  ordering?: string;
}): Promise<PaginatedResponse<Leg>> {
  const url = new URL(`${config.webServerBaseUrl}/api/tracks/legs/`);
  if (params?.journey) url.searchParams.set("journey", params.journey.toString());
  if (params?.ordering) url.searchParams.set("ordering", params.ordering);

  const response = await apiFetch(url.toString());
  if (!response.ok) {
    throw new Error(`Failed to fetch legs: ${response.statusText}`);
  }
  return await response.json();
}

export async function createLeg(leg: Partial<Leg>): Promise<Leg> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/legs/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(leg),
  });
  if (!response.ok) {
    throw new Error(`Failed to create leg: ${response.statusText}`);
  }
  return await response.json();
}

export async function updateLeg(id: number, leg: Partial<Leg>): Promise<Leg> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/legs/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(leg),
  });
  if (!response.ok) {
    throw new Error(`Failed to update leg ${id}: ${response.statusText}`);
  }
  return await response.json();
}

export async function deleteLeg(id: number): Promise<void> {
  const response = await apiFetch(`${config.webServerBaseUrl}/api/tracks/legs/${id}/`, {
    method: "DELETE",
  });
  if (!response.ok) {
    throw new Error(`Failed to delete leg ${id}: ${response.statusText}`);
  }
}
