/**
 * Great-circle distance between two GeoJSON coordinates, in nautical miles.
 *
 * Coordinates are `[longitude, latitude]` — the GeoJSON order used everywhere
 * on the client (see the root CLAUDE.md on coordinate conventions).
 *
 * This is a spherical approximation of the server's `ST_Length(...::geography)`,
 * which measures on the WGS84 spheroid. The two agree to within ~0.3%, which is
 * well inside GPS noise at the scale of a mode segment. Journey and leg figures
 * still come from the API; this exists because there is no per-segment endpoint.
 */

const EARTH_RADIUS_NM = 3440.065;

function toRadians(degrees: number): number {
  return (degrees * Math.PI) / 180;
}

export function distanceNm(a: [number, number], b: [number, number]): number {
  const [lngA, latA] = a;
  const [lngB, latB] = b;

  const lat1 = toRadians(latA);
  const lat2 = toRadians(latB);
  const dLat = lat2 - lat1;
  const dLng = toRadians(lngB - lngA);

  const h =
    Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLng / 2) ** 2;

  return 2 * EARTH_RADIUS_NM * Math.asin(Math.min(1, Math.sqrt(h)));
}
