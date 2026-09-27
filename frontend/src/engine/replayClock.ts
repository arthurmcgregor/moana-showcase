/**
 * Replay: journey time advances by wall-clock elapsed × a multiplier.
 *
 * Time stays continuous here. The cursor already snaps to the nearest fix when
 * it resolves a position, so the boat sits on a real fix without the playhead
 * having to stutter between them.
 *
 * Both the clock and the frame scheduler are injected. Nothing here reads
 * Date.now() or calls requestAnimationFrame directly, which is what lets it be
 * tested in the node environment the rest of the suite runs in.
 */

export const REPLAY_SPEEDS = [1, 60, 300, 900] as const;
export type ReplaySpeed = (typeof REPLAY_SPEEDS)[number];

export function nextReplaySpeed(current: number): ReplaySpeed {
  const index = REPLAY_SPEEDS.indexOf(current as ReplaySpeed);
  return REPLAY_SPEEDS[(index + 1) % REPLAY_SPEEDS.length];
}

export interface ReplayClockOptions {
  /** Wall-clock milliseconds; injected so tests can drive it by hand. */
  now: () => number;
  /** Schedules the next tick and returns a handle for cancel. */
  schedule: (tick: () => void) => number;
  cancel: (handle: number) => void;
  /** Journey time the playhead sits at now. */
  getTime: () => number | null;
  /** Where the playhead should move to. */
  setTime: (epochMs: number) => void;
  /** End of the journey — playback stops on reaching it. */
  getEndMs: () => number;
  onStop?: () => void;
}

export interface ReplayClock {
  start(multiplier: number): void;
  stop(): void;
  setMultiplier(multiplier: number): void;
  /** Advance as though `wallMs` of real time passed. Exposed for tests. */
  advance(wallMs: number): void;
  readonly isPlaying: () => boolean;
}

export function createReplayClock(options: ReplayClockOptions): ReplayClock {
  let handle: number | null = null;
  let multiplier = 1;
  let lastWallMs = 0;

  function stop() {
    if (handle !== null) {
      options.cancel(handle);
      handle = null;
      options.onStop?.();
    }
  }

  function advance(wallMs: number) {
    const current = options.getTime();
    if (current === null) {
      stop();
      return;
    }

    const next = current + wallMs * multiplier;
    const end = options.getEndMs();

    if (next >= end) {
      options.setTime(end);
      stop();
      return;
    }

    options.setTime(next);
  }

  function tick() {
    if (handle === null) return;
    const wallNow = options.now();
    const elapsed = wallNow - lastWallMs;
    lastWallMs = wallNow;
    advance(elapsed);
    // A stop inside advance() clears the handle; only keep going if it survived.
    if (handle !== null) handle = options.schedule(tick);
  }

  return {
    start(nextMultiplier: number) {
      if (handle !== null) return;
      multiplier = nextMultiplier;
      lastWallMs = options.now();
      handle = options.schedule(tick);
    },
    stop,
    setMultiplier(nextMultiplier: number) {
      multiplier = nextMultiplier;
    },
    advance,
    isPlaying: () => handle !== null,
  };
}
