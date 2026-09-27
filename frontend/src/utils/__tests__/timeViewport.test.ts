import { describe, it, expect } from "vitest";
import {
  FULL_VIEW,
  MAX_ZOOM,
  clampView,
  isFullView,
  followPlayhead,
  panBy,
  zoomAbout,
  zoomFactor,
} from "../timeViewport";

const near = (v: number) => Math.round(v * 1e6) / 1e6;
const shape = (v: { from: number; to: number }) => ({ from: near(v.from), to: near(v.to) });

describe("clampView", () => {
  it("keeps a viewport inside the journey", () => {
    expect(shape(clampView(-0.2, 0.3))).toEqual({ from: 0, to: 0.5 });
    expect(shape(clampView(0.8, 1.3))).toEqual({ from: 0.5, to: 1 });
  });

  it("slides rather than clips, so panning to the end does not zoom out", () => {
    // Width 0.25 survives the move; only the position changes.
    const slid = clampView(0.9, 1.15);
    expect(near(slid.to - slid.from)).toBe(0.25);
  });

  it("collapses anything at or beyond full width to the whole journey", () => {
    expect(clampView(-1, 2)).toEqual(FULL_VIEW);
    expect(clampView(0, 1)).toEqual(FULL_VIEW);
  });

  it("refuses to zoom past the limit", () => {
    const tight = clampView(0.5, 0.5);
    expect(near(tight.to - tight.from)).toBe(near(1 / MAX_ZOOM));
  });
});

describe("zoomAbout", () => {
  it("holds the point under the pointer still", () => {
    const view = zoomAbout(FULL_VIEW, 0.25, 0.5);
    // 0.25 of the journey was under the pointer and still is.
    expect(near(view.from + 0.25 * (view.to - view.from))).toBe(0.25);
  });

  it("halves the width when asked to", () => {
    const view = zoomAbout(FULL_VIEW, 0.5, 0.5);
    expect(near(view.to - view.from)).toBe(0.5);
    expect(shape(view)).toEqual({ from: 0.25, to: 0.75 });
  });

  it("zooms out to the whole journey and no further", () => {
    expect(zoomAbout({ from: 0.4, to: 0.6 }, 0.5, 10)).toEqual(FULL_VIEW);
  });

  it("stays in bounds when zooming out at an edge", () => {
    const view = zoomAbout({ from: 0, to: 0.2 }, 0, 2);
    expect(view.from).toBe(0);
    expect(near(view.to)).toBe(0.4);
  });
});

describe("panBy", () => {
  it("slides by a fraction of its own width", () => {
    expect(shape(panBy({ from: 0.4, to: 0.6 }, 0.5))).toEqual({ from: 0.5, to: 0.7 });
    expect(shape(panBy({ from: 0.4, to: 0.6 }, -0.5))).toEqual({ from: 0.3, to: 0.5 });
  });

  it("stops at the ends without narrowing", () => {
    const view = panBy({ from: 0.9, to: 1 }, 5);
    expect(shape(view)).toEqual({ from: 0.9, to: 1 });
  });
});

describe("followPlayhead", () => {
  it("leaves the view alone until the playhead reaches the anchor", () => {
    const view = { from: 0.4, to: 0.6 };
    expect(followPlayhead(view, 0.5)).toBe(view);
    expect(followPlayhead(view, 0.55)).toBe(view);
  });

  it("never moves the full journey", () => {
    expect(followPlayhead(FULL_VIEW, 0.9)).toBe(FULL_VIEW);
  });

  it("holds the playhead at the anchor and slides the view underneath", () => {
    let view: { from: number; to: number } = { from: 0.4, to: 0.6 };
    view = followPlayhead(view, 0.57);
    expect(near((0.57 - view.from) / (view.to - view.from))).toBe(0.8);
    expect(near(view.to - view.from)).toBe(0.2);

    // Every later frame keeps it pinned rather than jumping again.
    view = followPlayhead(view, 0.6);
    expect(near((0.6 - view.from) / (view.to - view.from))).toBe(0.8);
    expect(near(view.to - view.from)).toBe(0.2);
  });

  it("lets the playhead come off the anchor at the end of the journey", () => {
    // No room left to slide, so the needle finishes its travel to the edge.
    expect(followPlayhead({ from: 0.8, to: 1 }, 0.99)).toEqual({ from: 0.8, to: 1 });
  });

  it("brings the playhead back when it is behind the view", () => {
    const view = followPlayhead({ from: 0.5, to: 0.7 }, 0.2);
    expect(near((0.2 - view.from) / (view.to - view.from))).toBe(0.8);
  });
});

describe("isFullView / zoomFactor", () => {
  it("reads the whole journey as full", () => {
    expect(isFullView(FULL_VIEW)).toBe(true);
    expect(isFullView({ from: 0.1, to: 1 })).toBe(false);
  });

  it("reports how far in the viewport is", () => {
    expect(zoomFactor(FULL_VIEW)).toBe(1);
    expect(zoomFactor({ from: 0.25, to: 0.75 })).toBe(2);
  });
});
