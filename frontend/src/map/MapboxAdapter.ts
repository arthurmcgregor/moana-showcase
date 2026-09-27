import mapboxgl, {
  FullscreenControl,
  LngLatBounds,
  Marker,
  Popup,
  ScaleControl,
} from "mapbox-gl";
import type { Expression, IControl, Map } from "mapbox-gl";
import type { FeatureCollection, Feature, Point } from "geojson";
import type {
  AnimationControlModel,
  BoatMarkerPort,
  ControlPort,
  IconImageData,
  MapPort,
  PaintValue,
  PopupPort,
  Unsubscribe,
} from "./MapPort";
import { boatCanvas } from "./icons";

const PLAY_PATH =
  "M6.3 2.84A1.5 1.5 0 0 0 4 4.11v15.78a1.5 1.5 0 0 0 2.3 1.27l11.4-7.89a1.5 1.5 0 0 0 0-2.54L6.3 2.84Z";
const STOP_PATH =
  "M5.25 3A2.25 2.25 0 0 0 3 5.25v13.5A2.25 2.25 0 0 0 5.25 21h13.5A2.25 2.25 0 0 0 21 18.75V5.25A2.25 2.25 0 0 0 18.75 3H5.25Z";

function makeSvg(...paths: string[]): string {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width="18" height="18">${paths
    .map((d) => `<path d="${d}"/>`)
    .join("")}</svg>`;
}

function createAnimationIControl(model: AnimationControlModel): IControl {
  let container: HTMLDivElement;
  let playBtn: HTMLButtonElement;
  let speedBtn: HTMLButtonElement;
  let unsub: Unsubscribe | null = null;

  function render() {
    playBtn.innerHTML = model.isEnabled() ? makeSvg(STOP_PATH) : makeSvg(PLAY_PATH);
    playBtn.title = model.isEnabled() ? "Stop animation" : "Play animation";
    speedBtn.textContent = `${model.getSpeed()}×`;
    speedBtn.style.display = model.isEnabled() ? "" : "none";
  }

  return {
    onAdd() {
      container = document.createElement("div");
      container.className = "mapboxgl-ctrl mapboxgl-ctrl-group";

      playBtn = document.createElement("button");
      playBtn.type = "button";
      playBtn.style.cssText = "display:flex;align-items:center;justify-content:center;";
      playBtn.addEventListener("click", () => model.toggle());

      speedBtn = document.createElement("button");
      speedBtn.type = "button";
      speedBtn.style.cssText =
        "display:flex;align-items:center;justify-content:center;font-size:11px;font-weight:600;font-variant-numeric:tabular-nums;";
      speedBtn.addEventListener("click", () => model.cycleSpeed());

      container.appendChild(playBtn);
      container.appendChild(speedBtn);

      render();
      unsub = model.onStateChange(render);
      return container;
    },
    onRemove() {
      unsub?.();
      unsub = null;
      container.parentNode?.removeChild(container);
    },
    getDefaultPosition() {
      return "top-right";
    },
  } as IControl;
}

/**
 * A flat size cannot serve both ends of the zoom range: big enough to read
 * alongside, small enough not to blot the track with the whole day on screen.
 * The hull is drawn at 64 px, so every size on this ramp is a downsample.
 */
const BOAT_SIZE_BY_ZOOM: Array<[zoom: number, px: number]> = [
  [8, 18],
  [13, 27],
  [16, 37],
];

function boatSizeAt(zoom: number): number {
  const [firstZoom, firstPx] = BOAT_SIZE_BY_ZOOM[0];
  if (zoom <= firstZoom) return firstPx;
  for (let i = 1; i < BOAT_SIZE_BY_ZOOM.length; i++) {
    const [z, px] = BOAT_SIZE_BY_ZOOM[i];
    if (zoom > z) continue;
    const [prevZ, prevPx] = BOAT_SIZE_BY_ZOOM[i - 1];
    return prevPx + ((px - prevPx) * (zoom - prevZ)) / (z - prevZ);
  }
  return BOAT_SIZE_BY_ZOOM[BOAT_SIZE_BY_ZOOM.length - 1][1];
}

/**
 * The boat marker. One element, moved; the hull for each band of the speed scale
 * is drawn once and kept, so a change of band swaps a background rather than
 * redrawing anything.
 */
function createBoatMarker(map: Map): BoatMarkerPort {
  // Keyed by colour. A record rather than a Map because `Map` is Mapbox's here.
  // Only a drawn hull is cached — a canvas that failed to give up a context is
  // not a result worth remembering, and caching the empty string for it would
  // leave that one speed band invisible for the life of the map.
  const hulls: Record<string, string | undefined> = {};
  const element = document.createElement("div");
  element.style.backgroundRepeat = "no-repeat";
  element.style.backgroundSize = "100% 100%";
  element.style.pointerEvents = "none";

  // `rotationAlignment: "map"` is the marker's version of the layer's
  // `icon-rotation-alignment` — the hull points at a compass bearing, not at a
  // direction on screen.
  const marker = new Marker({ element, anchor: "center", rotationAlignment: "map" });
  let attached = false;
  let color: string | null = null;

  function resize() {
    const px = boatSizeAt(map.getZoom());
    element.style.width = `${px}px`;
    element.style.height = `${px}px`;
  }

  // No teardown: the adapter is built once per map in `onMapCreated`, so this
  // listener's lifetime is the map's and dies with it.
  map.on("zoom", resize);
  resize();

  return {
    show: (center, bearing, fill) => {
      if (fill !== color) {
        const hull = (hulls[fill] ??= boatCanvas(fill)?.toDataURL());
        if (hull) {
          element.style.backgroundImage = `url(${hull})`;
          color = fill;
        }
      }
      marker.setLngLat(center);
      marker.setRotation(bearing);
      if (!attached) {
        marker.addTo(map);
        attached = true;
      }
    },
    hide: () => {
      if (!attached) return;
      marker.remove();
      attached = false;
    },
  };
}

export function createMapboxAdapter(map: Map): MapPort {
  const popup: PopupPort = {
    show: (at, html) => {
      new Popup().setLngLat(at).setHTML(html).addTo(map);
    },
  };

  const controls: ControlPort = {
    // Left edge, both of them: fullscreen defaults to top right, which is where
    // the journey screen's readout lives.
    addFullscreen: () => {
      map.addControl(new FullscreenControl(), "top-left");
    },
    addScale: (unit) => {
      map.addControl(new ScaleControl({ unit }), "bottom-left");
    },
    addAnimationControl: (model) => {
      map.addControl(createAnimationIControl(model));
    },
  };

  return {
    hasSource: (id) => !!map.getSource(id),
    setSourceData: (id, data) => {
      const src = map.getSource(id) as mapboxgl.GeoJSONSource | undefined;
      if (src) src.setData(data as FeatureCollection | Feature);
    },
    hasLayer: (id) => !!map.getLayer(id),
    setPaintProperty: (layerId, prop, value: PaintValue) => {
      (map.setPaintProperty as (id: string, p: string, v: unknown) => void)(
        layerId,
        prop,
        value as Expression,
      );
    },
    addImage: (id, image: IconImageData, pixelRatio = 2) => {
      if (map.hasImage(id)) return;
      map.addImage(id, image, { pixelRatio });
    },
    hasImage: (id) => map.hasImage(id),
    fitBounds: (coords, padding, durationMs) => {
      if (coords.length === 0) return;
      const bounds = new LngLatBounds();
      for (const c of coords) bounds.extend(c);
      map.fitBounds(bounds, {
        padding: { top: padding, bottom: padding, left: padding, right: padding },
        duration: durationMs,
      });
    },
    easeTo: (center, durationMs) => {
      // `easeTo` does not shortcut a zero duration: it stops whatever transition
      // is running, builds an easing, and runs it to completion through the full
      // move/moveend cycle. Replay asks for the camera without a transition at
      // all, and that is what `jumpTo` is.
      if (durationMs === 0) {
        map.jumpTo({ center });
        return;
      }
      map.easeTo({ center, duration: durationMs, essential: true });
    },
    getZoom: () => map.getZoom(),
    setCursor: (style) => {
      map.getCanvas().style.cursor = style;
    },
    onSourceData: (cb) => {
      const handler = (e: mapboxgl.MapSourceDataEvent) => {
        cb({ sourceId: e.sourceId ?? "", isSourceLoaded: !!e.isSourceLoaded });
      };
      map.on("sourcedata", handler);
      return () => {
        map.off("sourcedata", handler);
      };
    },
    onLayerClick: (layerId, cb) => {
      const handler = (e: mapboxgl.MapLayerMouseEvent) => {
        if (!e.features || e.features.length === 0) return;
        const f = e.features[0];
        const coords = (f.geometry as Point).coordinates.slice() as [number, number];
        cb({ feature: f as Feature<Point>, lngLat: coords });
      };
      map.on("click", layerId, handler);
      return () => {
        map.off("click", layerId, handler);
      };
    },
    onLayerHover: (layerId, enter, leave) => {
      const en = () => enter();
      const le = () => leave();
      map.on("mouseenter", layerId, en);
      map.on("mouseleave", layerId, le);
      return () => {
        map.off("mouseenter", layerId, en);
        map.off("mouseleave", layerId, le);
      };
    },
    onMapClick: (cb) => {
      const handler = (e: mapboxgl.MapMouseEvent) => {
        cb({ lngLat: [e.lngLat.lng, e.lngLat.lat] });
      };
      map.on("click", handler);
      return () => {
        map.off("click", handler);
      };
    },
    popup,
    controls,
    boat: createBoatMarker(map),
  };
}
