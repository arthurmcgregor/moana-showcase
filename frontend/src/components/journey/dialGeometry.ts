/**
 * Where a dial's ink goes, for the parts of one that are drawn outside
 * `SegmentedDial` itself.
 *
 * The dial is an SVG on a fixed 200-unit box, so its centre is a constant rather
 * than anything measured, and every mark on it — a tick, a numeral, a compass
 * point, the wind's arrow — is a radius and an angle from there. The wind gauge
 * draws its compass into the dial's middle through the slot, which means it needs
 * the same centre and the same polar helper the dial's own scale is built from:
 * one copy here, rather than a second set of constants that agree until one of
 * them is edited.
 */

/** The box the dial is laid out on, and its middle. */
export const DIAL_SIZE = 200;
export const CENTRE = DIAL_SIZE / 2;

/** A point on the dial. 0° is to the right, and degrees run clockwise. */
export function polar(radius: number, deg: number): { x: number; y: number } {
  const rad = (deg * Math.PI) / 180;
  return { x: CENTRE + radius * Math.cos(rad), y: CENTRE + radius * Math.sin(rad) };
}
