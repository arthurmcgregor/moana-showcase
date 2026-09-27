import { modeTypes, annotationTypes } from "@/constants/journeyTypes";
import type { MapPort, IconImageData } from "./MapPort";

const iconCache = new Map<string, IconImageData>();

function cached(key: string, build: () => IconImageData | null): IconImageData | null {
  const hit = iconCache.get(key);
  if (hit) return hit;
  const built = build();
  if (built) iconCache.set(key, built);
  return built;
}

function markerImage(color: string, symbol: string): IconImageData | null {
  const canvas = document.createElement("canvas");
  canvas.width = 48;
  canvas.height = 48;
  const ctx = canvas.getContext("2d");
  if (!ctx) return null;
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(24, 24, 20, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 3;
  ctx.stroke();
  ctx.fillStyle = "#ffffff";
  ctx.font = "bold 20px Arial";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(symbol, 24, 24);
  const imageData = ctx.getImageData(0, 0, 48, 48);
  return { width: 48, height: 48, data: imageData.data };
}

function cameraImage(size = 48, options: { disk?: string } = {}): IconImageData | null {
  const canvas = document.createElement("canvas");
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext("2d");
  if (!ctx) return null;
  const cx = size / 2;
  const cy = size / 2;
  if (options.disk) {
    ctx.fillStyle = options.disk;
    ctx.beginPath();
    ctx.arc(cx, cy, size / 2 - 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = "#ffffff";
    ctx.lineWidth = 2.5;
    ctx.stroke();
  }
  ctx.fillStyle = "#ffffff";
  const bodyW = size * 0.55;
  const bodyH = size * 0.36;
  const bodyX = cx - bodyW / 2;
  const bodyY = cy - bodyH / 2 + 2;
  ctx.beginPath();
  ctx.roundRect(bodyX, bodyY, bodyW, bodyH, 3);
  ctx.fill();
  ctx.fillRect(cx - bodyW * 0.2, bodyY - 4, bodyW * 0.4, 4);
  if (options.disk) {
    ctx.fillStyle = options.disk;
  } else {
    ctx.globalCompositeOperation = "destination-out";
  }
  ctx.beginPath();
  ctx.arc(cx, bodyY + bodyH / 2 + 1, bodyH * 0.3, 0, Math.PI * 2);
  ctx.fill();
  ctx.globalCompositeOperation = "source-over";
  const imageData = ctx.getImageData(0, 0, size, size);
  return { width: size, height: size, data: imageData.data };
}

function crosshairImage(size = 64): IconImageData | null {
  const canvas = document.createElement("canvas");
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext("2d");
  if (!ctx) return null;
  ctx.strokeStyle = "#FF0000";
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.moveTo(size / 2, 0);
  ctx.lineTo(size / 2, size);
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(0, size / 2);
  ctx.lineTo(size, size / 2);
  ctx.stroke();
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.arc(size / 2, size / 2, size / 3, 0, Math.PI * 2);
  ctx.stroke();
  ctx.fillStyle = "#FFFFFF";
  ctx.beginPath();
  ctx.arc(size / 2, size / 2, 4, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = "#FF0000";
  ctx.lineWidth = 2;
  ctx.stroke();
  const imageData = ctx.getImageData(0, 0, size, size);
  return { width: size, height: size, data: imageData.data };
}

/**
 * The boat: a hull seen from above — bow, sheer, transom — drawn pointing north
 * so the marker's rotation can turn it to the fix's course. It is asymmetric
 * fore-and-aft, so heading reads from the silhouette alone without needing a
 * separate arrow.
 *
 * Drawn at 64 and worn at half that or less, which is what keeps the outline
 * crisp: the hull is only ever downsampled, never stretched.
 *
 * `fill` is the speed colour of the fix the boat is standing on, so the hull
 * reads as part of the track rather than an object on top of it. That is also
 * why the outline is two-toned: a single white outline disappears against the
 * pale end of the scale and a single dark one against the deep end, while
 * white-outside-dark reads on every band and on both water and land.
 *
 * Unlike everything else here this returns the canvas rather than its pixels —
 * it dresses a DOM marker, not a registered map image. See `BoatMarkerPort`.
 */
export function boatCanvas(fill: string, size = 64): HTMLCanvasElement | null {
  const canvas = document.createElement("canvas");
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext("2d");
  if (!ctx) return null;

  const cx = size / 2;
  const beam = size * 0.23;

  function hullPath() {
    if (!ctx) return;
    ctx.beginPath();
    ctx.moveTo(cx, size * 0.07);
    ctx.bezierCurveTo(
      cx + beam * 0.95, size * 0.34,
      cx + beam, size * 0.64,
      cx + beam * 0.8, size * 0.9,
    );
    ctx.lineTo(cx - beam * 0.8, size * 0.9);
    ctx.bezierCurveTo(
      cx - beam, size * 0.64,
      cx - beam * 0.95, size * 0.34,
      cx, size * 0.07,
    );
    ctx.closePath();
  }

  ctx.lineJoin = "round";

  // Outer white casing first, then the dark edge over it, so the dark line lands
  // on the hull's true outline rather than outside it.
  hullPath();
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = size * 0.11;
  ctx.stroke();

  hullPath();
  ctx.fillStyle = fill;
  ctx.fill();
  ctx.strokeStyle = "rgba(15,23,42,0.85)";
  ctx.lineWidth = size * 0.045;
  ctx.stroke();

  // A bar across the transom: the flat end is the stern, and saying so explicitly
  // is what keeps the heading unambiguous once the hull is only ~20 px long.
  ctx.beginPath();
  ctx.moveTo(cx - beam * 0.62, size * 0.84);
  ctx.lineTo(cx + beam * 0.62, size * 0.84);
  ctx.strokeStyle = "rgba(15,23,42,0.85)";
  ctx.lineWidth = size * 0.05;
  ctx.stroke();

  return canvas;
}

export function loadMapIcons(port: MapPort): void {
  for (const [type, info] of Object.entries(annotationTypes)) {
    const id = `${type.toLowerCase()}-icon`;
    const img = cached(id, () => markerImage(info.color, info.icon));
    if (img) port.addImage(id, img, 2);
  }
  for (const [mode, info] of Object.entries(modeTypes)) {
    const id = `${mode.toLowerCase()}-mode-icon`;
    const img = cached(id, () => markerImage(info.color, info.icon));
    if (img) port.addImage(id, img, 2);
  }
  const ch = cached("crosshair", () => crosshairImage());
  if (ch) port.addImage("crosshair", ch, 2);
  const cam = cached("camera-icon", () => cameraImage(48, { disk: "#14b8a6" }));
  if (cam) port.addImage("camera-icon", cam, 2);
  const camGlyph = cached("camera-glyph", () => cameraImage(48));
  if (camGlyph) port.addImage("camera-glyph", camGlyph, 2);
}
