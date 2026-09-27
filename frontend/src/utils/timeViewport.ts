/**
 * The stretch of a journey the deck is currently drawing, as fractions of the
 * whole: `{ from: 0, to: 1 }` is the entire journey, `{ from: 0.5, to: 0.75 }`
 * the third quarter at four times the width.
 *
 * Fractions rather than timestamps so the viewport survives the journey's bounds
 * moving underneath it — a mode edit that extends the last segment should not
 * shift what you were looking at.
 */
export interface TimeViewport {
  from: number;
  to: number;
}

export const FULL_VIEW: TimeViewport = { from: 0, to: 1 };

/** Past this the bars are drawing a few seconds and there is nothing left to see. */
export const MAX_ZOOM = 60;

const MIN_WIDTH = 1 / MAX_ZOOM;

export function isFullView(view: TimeViewport): boolean {
  return view.from <= 0 && view.to >= 1;
}

export function zoomFactor(view: TimeViewport): number {
  return 1 / Math.max(view.to - view.from, MIN_WIDTH);
}

/**
 * A viewport held inside the journey and inside the zoom limits, keeping its
 * width where it can. Sliding a window back into bounds rather than clipping it
 * is what stops a pan at either end from also zooming out.
 */
export function clampView(from: number, to: number): TimeViewport {
  const width = Math.min(Math.max(to - from, MIN_WIDTH), 1);
  if (width >= 1) return { ...FULL_VIEW };
  const start = Math.min(Math.max(from, 0), 1 - width);
  return { from: start, to: start + width };
}

/**
 * Zoom about a point, which stays put while everything else moves around it —
 * so the boundary under the pointer is the one you keep, and zooming in on it
 * takes one gesture rather than a zoom and then a hunt.
 *
 * `scale` is a width multiplier: below 1 zooms in, above 1 out.
 */
export function zoomAbout(view: TimeViewport, pointer: number, scale: number): TimeViewport {
  const width = view.to - view.from;
  const anchor = view.from + Math.min(Math.max(pointer, 0), 1) * width;
  const next = Math.min(Math.max(width * scale, MIN_WIDTH), 1);
  const from = anchor - Math.min(Math.max(pointer, 0), 1) * next;
  return clampView(from, from + next);
}

/** Slide the viewport by a fraction of its own width. */
export function panBy(view: TimeViewport, deltaWidths: number): TimeViewport {
  const width = view.to - view.from;
  const shift = deltaWidths * width;
  return clampView(view.from + shift, view.to + shift);
}

/**
 * Hold the playhead still and slide the journey underneath it.
 *
 * Replay advances the playhead, so something has to give once it reaches the
 * edge. Re-centring in one step makes the bars jump, and a jump is the one thing
 * you cannot read: it moves every landmark at the moment you were reading them.
 * Instead, once the playhead reaches `anchor` it stays exactly there and the
 * viewport is recomputed around it each frame, which reads as the journey being
 * drawn past a fixed needle.
 *
 * Before it reaches the anchor the viewport does not move at all — playing from
 * a spot you chose should not immediately drag the view off it.
 *
 * At the end of the journey the viewport runs out of room to slide, so the
 * playhead comes off its anchor and finishes its travel to the right-hand edge.
 * That falls out of the clamp rather than being a case of its own.
 */
export function followPlayhead(view: TimeViewport, at: number, anchor = 0.8): TimeViewport {
  if (isFullView(view)) return view;
  const width = view.to - view.from;
  const position = (at - view.from) / width;

  // Behind the view entirely — a seek backwards, not replay drifting forward.
  if (position < 0) return clampView(at - anchor * width, at - anchor * width + width);
  if (position < anchor) return view;

  const from = at - anchor * width;
  return clampView(from, from + width);
}
