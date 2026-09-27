/**
 * The speed colour scale, in one place because two things read it now: the track
 * dots colour themselves through a Mapbox paint expression, and the boat marker
 * takes a fill so the hull can wear the speed it is doing. A second copy of
 * these numbers would let the boat disagree with the dots underneath it.
 */

export const SPEED_THRESHOLDS: Record<string, number[]> = {
  walking: [0, 1, 1.5, 2, 2.5, 3, 3.5],
  biking: [0, 5, 8, 11, 13, 16, 19],
  driving: [0, 11, 22, 32, 43, 49, 54],
  sailing: [0, 3, 5, 8, 11, 13, 15],
};

export const SPEED_COLORS = [
  "#d7191c",
  "#fdae61",
  "#ffffbf",
  "#a6d96a",
  "#1a9641",
  "#6a51a3",
  "#d1b3ff",
];

export function thresholdsFor(mode: string | undefined): number[] {
  return (
    SPEED_THRESHOLDS[(mode ?? "sailing") as keyof typeof SPEED_THRESHOLDS]
    ?? SPEED_THRESHOLDS.sailing
  );
}

/**
 * Which colour band a speed falls in. The dots interpolate between bands and the
 * boat cannot — the hull is one flat colour — so it wears the colour of the band
 * it has reached, which is the nearest dot colour below it.
 */
export function speedBucket(speed: number | null | undefined, mode: string | undefined): number {
  if (typeof speed !== "number" || Number.isNaN(speed)) return 0;
  const thresholds = thresholdsFor(mode);
  let bucket = 0;
  for (let i = 0; i < thresholds.length; i++) {
    if (speed >= thresholds[i]) bucket = i;
  }
  return bucket;
}
