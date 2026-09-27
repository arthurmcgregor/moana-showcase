import { config } from "../config";

/**
 * One track from the device's `/tracks/list`. `size` is the file size in bytes,
 * or `null` when the device is on older firmware that lists names only.
 */
export interface DeviceTrack {
  name: string;
  size: number | null;
}

/**
 * Normalises the two shapes `/tracks/list` can return into `DeviceTrack[]`:
 *   - new firmware: `{ tracks: [{ name, size }, ...] }`
 *   - old firmware: `{ tracks: ["name", ...] }` (size unknown → null)
 * Throws on an unrecognised payload so the caller surfaces a clear error.
 */
export function parseTrackList(data: unknown): DeviceTrack[] {
  const tracks = (data as { tracks?: unknown } | null)?.tracks;
  if (!Array.isArray(tracks)) {
    throw new Error("Invalid response format from /tracks/list");
  }

  return tracks.map((entry): DeviceTrack => {
    if (typeof entry === "string") {
      return { name: entry, size: null };
    }
    if (entry && typeof entry === "object" && typeof (entry as { name?: unknown }).name === "string") {
      const { name, size } = entry as { name: string; size?: unknown };
      return { name, size: typeof size === "number" ? size : null };
    }
    throw new Error("Invalid track entry in /tracks/list");
  });
}

/** Error carrying the HTTP status of a failed device request. */
export class DeviceRequestError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.name = "DeviceRequestError";
    this.status = status;
  }
}

function getCsrfToken(): string | undefined {
  return document.cookie
    .split("; ")
    .find((row) => row.startsWith("csrftoken="))
    ?.split("=")[1];
}

/**
 * fetch wrapper for device requests. When the device is reached via the Django
 * proxy (hosted HTTPS app), unsafe methods need the CSRF token + session cookie;
 * when talking to the ESP32 directly these are simply absent and ignored.
 */
function deviceFetch(url: string, init?: RequestInit): Promise<Response> {
  const method = init?.method?.toUpperCase();
  const headers = new Headers(init?.headers);
  if (method && method !== "GET" && method !== "HEAD") {
    const csrf = getCsrfToken();
    if (csrf) headers.set("X-CSRFToken", csrf);
  }
  return fetch(url, { credentials: "include", ...init, headers });
}

/**
 * Fetches the list of available track files from the webserver
 */
export async function fetchTrackList(): Promise<DeviceTrack[]> {
  const response = await deviceFetch(`${config.deviceServerBaseUrl}/tracks/list`);

  if (!response.ok) {
    throw new DeviceRequestError(`Failed to fetch track list: ${response.statusText}`, response.status);
  }

  return parseTrackList(await response.json());
}

/**
 * Fetches a specific track CSV file from the webserver
 * @param filename The name of the track file to fetch
 */
export async function fetchTrackCsv(filename: string): Promise<string> {
  const url = `${config.deviceServerBaseUrl}/track?file=${encodeURIComponent(filename)}`;
  const response = await deviceFetch(url);

  if (!response.ok) {
    throw new DeviceRequestError(`Failed to fetch track "${filename}": ${response.statusText}`, response.status);
  }

  return await response.text();
}

/** Derives the watch-events sidecar name from a track CSV name: "gps_X.csv" → "gps_X.events.jsonl". */
export function eventSidecarName(filename: string): string {
  return filename.replace(/\.csv$/i, "") + ".events.jsonl";
}

/**
 * Fetches the watch-events JSONL sidecar for a track, or null when there isn't
 * one. Most tracks are recorded without the watch, so any failure (404, network
 * error) means "no events" — the caller proceeds with a plain CSV upload.
 * @param filename The name of the track CSV the sidecar belongs to
 */
export async function fetchTrackEvents(filename: string): Promise<string | null> {
  const sidecar = eventSidecarName(filename);
  try {
    const response = await deviceFetch(`${config.deviceServerBaseUrl}/track?file=${encodeURIComponent(sidecar)}`);
    if (!response.ok) return null;
    return await response.text();
  } catch {
    return null;
  }
}

/**
 * Deletes a track from the device
 * @param filename The name of the track file to delete
 */
export async function deleteDeviceTrack(filename: string): Promise<void> {
  const response = await deviceFetch(
    `${config.deviceServerBaseUrl}/track?file=${encodeURIComponent(filename)}`,
    {
      method: "DELETE",
    },
  );

  if (!response.ok) {
    throw new DeviceRequestError(`Failed to delete track: ${response.statusText}`, response.status);
  }

  // Best-effort sidecar cleanup so a deleted track doesn't strand its events
  // file on the device. Failure (usually 404 — no watch events) is not an error.
  if (/\.csv$/i.test(filename)) {
    try {
      await deviceFetch(
        `${config.deviceServerBaseUrl}/track?file=${encodeURIComponent(eventSidecarName(filename))}`,
        { method: "DELETE" },
      );
    } catch {
      // ignore — the sidecar is optional
    }
  }
}

export function isNetworkError(error: unknown): boolean {
  // Direct-to-device: the browser can't reach the ESP32 → fetch throws a TypeError.
  if (error instanceof TypeError && /network|fetch/i.test(error.message)) return true;
  // Via the Django proxy (hosted app): an unreachable/unconfigured device is
  // reported as 502/503 rather than a fetch-level failure — same recoverable state.
  if (error instanceof DeviceRequestError && (error.status === 502 || error.status === 503)) return true;
  return false;
}

/**
 * A 500 from the device means its SD card is missing or failed — the firmware
 * returns {"error":"SD card unavailable (missing or failed)"} in exactly that
 * case (see the device web server's handleTracksList). Recoverable: the user
 * inserts a card and retries, so we surface a specific hint rather than a raw
 * "Internal Server Error".
 */
export function isNoSdCardError(error: unknown): boolean {
  return error instanceof DeviceRequestError && error.status === 500;
}
