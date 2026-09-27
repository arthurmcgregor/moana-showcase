import type { Mode, Annotation, GeoJSONFeature, GeoJSONFeatureCollection } from "@/api/web";

export interface TimelineData {
  features: GeoJSONFeatureCollection;
  modes: Mode[];
  annotations: Annotation[];
  journeyStart: string; // ISO
  journeyEnd: string | null; // ISO
}

export interface SnappedPoint {
  index: number;
  feature: GeoJSONFeature;
}

export interface TimelineEngine {
  // Conversions
  timeToPercentage(epochMs: number): number;
  percentageToTime(percentage: number): number;

  // Point lookups
  snapToPoint(epochMs: number): SnappedPoint | null;
  /** The first sample at or after `epochMs`, for landing inside a window. */
  snapToPointAtOrAfter(epochMs: number): SnappedPoint | null;
  pointAtIndex(idx: number): SnappedPoint | null;
  modeAtTime(epochMs: number): Mode | null;
  modeBefore(epochMs: number): Mode | null;
  modeColorAtTime(epochMs: number): string;
  isNearModeStart(epochMs: number): boolean;
  modeBoundaryAfter(epochMs: number): number | null;
  modeBoundaryBefore(epochMs: number): number | null;
  annotationsNear(epochMs: number, windowMs?: number): Annotation[];

  // Spatial
  nearestPointToCoordinate(coord: [number, number]): SnappedPoint | null;

  // Visual
  modeGradientCSS(): string;

  // Batch (sweep algorithm for SpeedBarGraph)
  modeColorsForFeatures(features: GeoJSONFeature[]): string[];

  // Metadata
  readonly startMs: number;
  readonly endMs: number;
}
