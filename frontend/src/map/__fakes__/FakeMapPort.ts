import type { FeatureCollection, Feature } from "geojson";
import type {
  AnimationControlModel,
  LayerClickEvent,
  MapPort,
  PaintValue,
  SourceDataEvent,
  Unsubscribe,
} from "../MapPort";
import { LAYER } from "../orchestrator";

/** One call to the boat marker: where it was put, or `null` for a hide. */
export type BoatShow = { center: [number, number]; bearing: number; color: string } | null;

export interface FakeMapPortHandle {
  port: MapPort;
  /** Every boat call in order — the count matters as much as the last one. */
  boatShows: BoatShow[];
  boat: BoatShow;
  sources: Map<string, FeatureCollection | Feature>;
  paint: Record<string, Record<string, PaintValue>>;
  fits: Array<{ coords: Array<[number, number]>; padding: number; durationMs: number }>;
  eases: Array<{ center: [number, number]; durationMs: number }>;
  zoom: number;
  cursor: string;
  images: Set<string>;
  addedControls: string[];
  animationControl: AnimationControlModel | null;
  listenerCount: number;

  emitSourceData(event: SourceDataEvent): void;
  emitLayerClick(layerId: string, event: LayerClickEvent): void;
  emitMapClick(lngLat: [number, number]): void;
  lastPopup: { at: [number, number]; html: string } | null;
}

export function createFakeMapPort(): FakeMapPortHandle {
  const sources = new Map<string, FeatureCollection | Feature>();
  const paint: Record<string, Record<string, PaintValue>> = {};
  const fits: FakeMapPortHandle["fits"] = [];
  const eases: FakeMapPortHandle["eases"] = [];
  let zoom = 12;
  const images = new Set<string>();
  const addedControls: string[] = [];
  const existingIds = new Set<string>([
    LAYER.POINTS,
    LAYER.CURSOR,
    LAYER.ANNOTATIONS,
    LAYER.MODES,
  ]);

  const sourceDataCbs: Array<(e: SourceDataEvent) => void> = [];
  const layerClickCbs = new Map<string, Array<(e: LayerClickEvent) => void>>();
  const mapClickCbs: Array<(e: { lngLat: [number, number] }) => void> = [];

  let cursor = "";
  const boatShows: BoatShow[] = [];
  let animationControl: AnimationControlModel | null = null;
  let lastPopup: { at: [number, number]; html: string } | null = null;

  const port: MapPort = {
    hasSource: (id) => existingIds.has(id),
    setSourceData: (id, data) => {
      sources.set(id, data);
    },
    hasLayer: (id) => existingIds.has(id),
    setPaintProperty: (layerId, prop, value) => {
      (paint[layerId] ??= {})[prop] = value;
    },
    addImage: (id) => {
      images.add(id);
    },
    hasImage: (id) => images.has(id),
    fitBounds: (coords, padding, durationMs) => {
      fits.push({ coords: coords.map((c) => [...c] as [number, number]), padding, durationMs });
    },
    easeTo: (center, durationMs) => {
      eases.push({ center: [...center] as [number, number], durationMs });
    },
    getZoom: () => zoom,
    setCursor: (style) => {
      cursor = style;
    },
    onSourceData: (cb): Unsubscribe => {
      sourceDataCbs.push(cb);
      return () => {
        const idx = sourceDataCbs.indexOf(cb);
        if (idx >= 0) sourceDataCbs.splice(idx, 1);
      };
    },
    onLayerClick: (layerId, cb): Unsubscribe => {
      const list = layerClickCbs.get(layerId) ?? [];
      list.push(cb);
      layerClickCbs.set(layerId, list);
      return () => {
        const arr = layerClickCbs.get(layerId);
        if (!arr) return;
        const idx = arr.indexOf(cb);
        if (idx >= 0) arr.splice(idx, 1);
      };
    },
    onLayerHover: (): Unsubscribe => {
      return () => {};
    },
    onMapClick: (cb): Unsubscribe => {
      mapClickCbs.push(cb);
      return () => {
        const idx = mapClickCbs.indexOf(cb);
        if (idx >= 0) mapClickCbs.splice(idx, 1);
      };
    },
    popup: {
      show: (at, html) => {
        lastPopup = { at, html };
      },
    },
    boat: {
      show: (center, bearing, color) => {
        boatShows.push({ center: [...center] as [number, number], bearing, color });
      },
      hide: () => {
        boatShows.push(null);
      },
    },
    controls: {
      addFullscreen: () => addedControls.push("fullscreen"),
      addScale: () => addedControls.push("scale"),
      addAnimationControl: (model) => {
        animationControl = model;
        addedControls.push("animation");
      },
    },
  };

  return {
    port,
    sources,
    paint,
    fits,
    eases,
    boatShows,
    get boat() {
      return boatShows.length ? boatShows[boatShows.length - 1] : null;
    },
    get zoom() {
      return zoom;
    },
    set zoom(next: number) {
      zoom = next;
    },
    get cursor() {
      return cursor;
    },
    images,
    addedControls,
    get animationControl() {
      return animationControl;
    },
    get listenerCount() {
      let n = sourceDataCbs.length + mapClickCbs.length;
      for (const list of layerClickCbs.values()) n += list.length;
      return n;
    },
    get lastPopup() {
      return lastPopup;
    },
    emitSourceData(e) {
      for (const cb of [...sourceDataCbs]) cb(e);
    },
    emitLayerClick(layerId, e) {
      const list = layerClickCbs.get(layerId);
      if (!list) return;
      for (const cb of [...list]) cb(e);
    },
    emitMapClick(lngLat) {
      for (const cb of [...mapClickCbs]) cb({ lngLat });
    },
  };
}
