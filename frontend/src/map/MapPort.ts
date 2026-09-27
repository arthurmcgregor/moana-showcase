import type { FeatureCollection, Feature, Point } from "geojson";

export type PaintValue = string | number | boolean | unknown[];

export interface SourceDataEvent {
  sourceId: string;
  isSourceLoaded: boolean;
}

export interface LayerClickEvent {
  feature: Feature<Point>;
  lngLat: [number, number];
}

export interface MapClickEvent {
  lngLat: [number, number];
}

export interface IconImageData {
  width: number;
  height: number;
  data: Uint8ClampedArray;
}

export type Unsubscribe = () => void;

export interface AnimationControlModel {
  isEnabled(): boolean;
  getSpeed(): number;
  toggle(): void;
  cycleSpeed(): void;
  onStateChange(cb: () => void): Unsubscribe;
}

export interface PopupPort {
  show(at: [number, number], html: string): void;
}

/**
 * The boat is a marker rather than a layer, alone among the things on the map.
 *
 * Everything else here sits still, so handing it to the map as source data —
 * off to a worker, tiled, laid out, back — costs nothing but a little latency
 * once. The boat moves under a moving camera, and that latency is then visible:
 * the camera is set on the main thread and lands this frame, the hull comes back
 * from the worker a frame or more later, and the two disagree for as long as
 * replay runs. A marker is positioned on the main thread against the same camera
 * in the same frame, so it cannot lag behind it.
 */
export interface BoatMarkerPort {
  /** Place the hull, turned to a bearing, wearing the fix's speed colour. */
  show(center: [number, number], bearing: number, color: string): void;
  hide(): void;
}

export interface ControlPort {
  addFullscreen(): void;
  addScale(unit: "nautical"): void;
  addAnimationControl(model: AnimationControlModel): void;
}

export interface MapPort {
  hasSource(id: string): boolean;
  setSourceData(id: string, data: FeatureCollection | Feature): void;

  hasLayer(id: string): boolean;
  setPaintProperty(layerId: string, prop: string, value: PaintValue): void;

  addImage(id: string, image: IconImageData, pixelRatio?: number): void;
  hasImage(id: string): boolean;

  fitBounds(coords: Array<[number, number]>, padding: number, durationMs: number): void;
  /** Pan to a centre without touching zoom — how replay follows the boat. */
  easeTo(center: [number, number], durationMs: number): void;
  getZoom(): number;
  setCursor(style: "pointer" | ""): void;

  onSourceData(cb: (e: SourceDataEvent) => void): Unsubscribe;
  onLayerClick(layerId: string, cb: (e: LayerClickEvent) => void): Unsubscribe;
  onLayerHover(layerId: string, enter: () => void, leave: () => void): Unsubscribe;
  onMapClick(cb: (e: MapClickEvent) => void): Unsubscribe;

  popup: PopupPort;
  controls: ControlPort;
  boat: BoatMarkerPort;
}
