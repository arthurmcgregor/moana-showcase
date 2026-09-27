export interface ModeTypeInfo {
  label: string;
  icon: string;
  color: string;
}

import type { AnnotationType } from "@/api/web";

export interface AnnotationTypeInfo {
  label: string;
  icon: string;
  color: string;
}

export const modeTypes: Record<string, ModeTypeInfo> = {
  SAIL: { label: "Sailing", icon: "Sail", color: "#0d6efd" },
  MOTORSAIL: { label: "Motor Sailing", icon: "M/S", color: "#6610f2" },
  MOTOR: { label: "Motoring", icon: "Motor", color: "#dc3545" },
  ROW: { label: "Rowing", icon: "Row", color: "#20c997" },
  UNPOWERED: { label: "Drifting/Unpowered", icon: "Drift", color: "#adb5bd" },
  ANCHOR: { label: "Anchored", icon: "Anchor", color: "#fd7e14" },
  ALONGSIDE: { label: "Alongside/Docked", icon: "Dock", color: "#6f42c1" },
  BEACHED: { label: "Beached", icon: "Beach", color: "#ffc107" },
  TRAILERED: { label: "On Trailer", icon: "Trl", color: "#198754" },
};

export const annotationTypes: Record<AnnotationType, AnnotationTypeInfo> = {
  TACK: { label: "Tack", icon: "Tack", color: "#17a2b8" },
  GYBE: { label: "Gybe", icon: "Gybe", color: "#6610f2" },
  COURSE_CHANGE: { label: "Course Change", icon: "Crs", color: "#0d6efd" },
  NOTE: { label: "General Note", icon: "Note", color: "#6c757d" },
};

export const modeTypeList = Object.entries(modeTypes).map(([value, info]) => ({
  value,
  ...info,
}));

export const annotationTypeList = (Object.entries(annotationTypes) as [AnnotationType, AnnotationTypeInfo][]).map(([value, info]) => ({
  value,
  ...info,
}));

export function getModeColor(mode: string): string {
  return modeTypes[mode]?.color ?? "#6c757d";
}

export function getModeIcon(mode: string): string {
  return modeTypes[mode]?.icon ?? "?";
}

export function getAnnotationColor(type: AnnotationType): string {
  return annotationTypes[type]?.color ?? "#6c757d";
}

export function getAnnotationIcon(type: AnnotationType): string {
  return annotationTypes[type]?.icon ?? "?";
}
