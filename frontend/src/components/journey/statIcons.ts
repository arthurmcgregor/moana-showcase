/**
 * One family of marks for the stats panel, so a figure is found by shape rather
 * than by reading every label.
 *
 * Heroicons has no thermometer, wave, tilt or speedometer, and a set half
 * borrowed would read as an accident — so these are drawn on heroicons' own 24
 * grid at its own stroke weight, and the ones it does carry are traced here
 * rather than imported, to keep the whole row consistent.
 */

export type StatIconName =
  | "distance"
  | "duration"
  | "speed"
  | "tacks"
  | "heel"
  | "temperature"
  | "tide"
  | "wind"
  | "gust";

export const STAT_ICON_PATHS: Record<StatIconName, string> = {
  // A course over ground: a leg out and a leg back.
  distance: "M3 18l5.5-12L13 15l3-6 5 9",
  duration: "M12 3a9 9 0 100 18 9 9 0 000-18zm0 4.5V12l3 2.25",
  // A needle swung up its arc.
  speed: "M4 17a8 8 0 1116 0M12 17l4.5-6",
  tacks: "M3 8h14l-3-3m3 3l-3 3M21 16H7l3-3m-3 3l3 3",
  // A mast off vertical over a waterline.
  heel: "M3 20h18M15 20L9.5 4M9.5 4L7 9.5M9.5 4l4 3.5",
  temperature: "M10 14.5V5a2 2 0 114 0v9.5a4 4 0 11-4 0z",
  // The swell the tide rides on.
  tide: "M3 9c2.5-2.5 5-2.5 7.5 0S15.5 11.5 21 9M3 15c2.5-2.5 5-2.5 7.5 0S15.5 17.5 21 15",
  // Two streamlines blowing off to the right.
  wind: "M3 8h11a3 3 0 100-6M3 16h7a2.5 2.5 0 110 5",
  gust: "M13 3L5 13.5h5L9 21l8-10.5h-5L13 3z",
};
