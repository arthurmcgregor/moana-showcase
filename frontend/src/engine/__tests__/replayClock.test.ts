import { describe, it, expect } from "vitest";
import { createReplayClock, nextReplaySpeed, REPLAY_SPEEDS } from "../replayClock";

/**
 * A hand-driven scheduler: nothing runs until the test says a frame happened,
 * so replay is exercised without timers or a browser.
 */
function harness(options: { start: number; end: number }) {
  let wallMs = 0;
  let time: number | null = options.start;
  let pending: (() => void) | null = null;
  let nextHandle = 1;
  const stops: number[] = [];

  const clock = createReplayClock({
    now: () => wallMs,
    schedule: (tick) => {
      pending = tick;
      return nextHandle++;
    },
    cancel: () => {
      pending = null;
    },
    getTime: () => time,
    setTime: (ms) => {
      time = ms;
    },
    getEndMs: () => options.end,
    onStop: () => stops.push(time ?? -1),
  });

  return {
    clock,
    stops,
    getTime: () => time,
    clearTime: () => {
      time = null;
    },
    /** One animation frame, `wall` milliseconds after the last. */
    frame(wall: number) {
      wallMs += wall;
      const tick = pending;
      pending = null;
      tick?.();
    },
  };
}

const T0 = 1_000_000;
const MINUTE = 60_000;

describe("createReplayClock", () => {
  it("advances journey time by elapsed wall time at 1×", () => {
    const h = harness({ start: T0, end: T0 + 10 * MINUTE });

    h.clock.start(1);
    h.frame(1000);

    expect(h.getTime()).toBe(T0 + 1000);
  });

  it("multiplies elapsed time by the replay speed", () => {
    const h = harness({ start: T0, end: T0 + 60 * MINUTE });

    h.clock.start(300);
    h.frame(1000);

    expect(h.getTime()).toBe(T0 + 300_000);
  });

  it("keeps running frame after frame", () => {
    const h = harness({ start: T0, end: T0 + 10 * MINUTE });

    h.clock.start(60);
    h.frame(100);
    h.frame(100);
    h.frame(100);

    expect(h.getTime()).toBe(T0 + 18_000);
    expect(h.clock.isPlaying()).toBe(true);
  });

  it("takes a speed change from the next frame on", () => {
    const h = harness({ start: T0, end: T0 + 60 * MINUTE });

    h.clock.start(1);
    h.frame(1000);
    h.clock.setMultiplier(60);
    h.frame(1000);

    expect(h.getTime()).toBe(T0 + 1000 + 60_000);
  });

  it("stops on the journey's end rather than running past it", () => {
    const end = T0 + 2000;
    const h = harness({ start: T0, end });

    h.clock.start(1);
    h.frame(5000);

    expect(h.getTime()).toBe(end);
    expect(h.clock.isPlaying()).toBe(false);
    expect(h.stops).toEqual([end]);
  });

  it("stops when the cursor is cleared underneath it", () => {
    const h = harness({ start: T0, end: T0 + 10 * MINUTE });

    h.clock.start(1);
    h.clearTime();
    h.frame(1000);

    expect(h.clock.isPlaying()).toBe(false);
  });

  it("ignores a second start while already playing", () => {
    const h = harness({ start: T0, end: T0 + 10 * MINUTE });

    h.clock.start(1);
    h.clock.start(900);
    h.frame(1000);

    expect(h.getTime()).toBe(T0 + 1000);
  });

  it("stops cleanly and stays stopped", () => {
    const h = harness({ start: T0, end: T0 + 10 * MINUTE });

    h.clock.start(1);
    h.clock.stop();
    h.frame(1000);

    expect(h.getTime()).toBe(T0);
    expect(h.clock.isPlaying()).toBe(false);
  });

  it("reports a stop only once", () => {
    const h = harness({ start: T0, end: T0 + 10 * MINUTE });
    h.clock.start(1);
    h.clock.stop();
    h.clock.stop();

    expect(h.stops).toHaveLength(1);
  });

  it("does not drift when frames arrive unevenly", () => {
    const h = harness({ start: T0, end: T0 + 60 * MINUTE });
    const uneven = [7, 33, 12, 91, 4];

    h.clock.start(60);
    for (const wall of uneven) h.frame(wall);

    const total = uneven.reduce((a, b) => a + b, 0);
    expect(h.getTime()).toBe(T0 + total * 60);
  });
});

describe("nextReplaySpeed", () => {
  it("cycles through the fixed multiples and back to 1×", () => {
    expect(REPLAY_SPEEDS).toEqual([1, 60, 300, 900]);
    expect(nextReplaySpeed(1)).toBe(60);
    expect(nextReplaySpeed(60)).toBe(300);
    expect(nextReplaySpeed(300)).toBe(900);
    expect(nextReplaySpeed(900)).toBe(1);
  });

  it("recovers from a speed that is not on the list", () => {
    expect(nextReplaySpeed(42)).toBe(1);
  });
});
