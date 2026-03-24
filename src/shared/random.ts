/**
 * Small random-source helpers used where deterministic browser-side sampling is
 * still needed for benchmarks or archived-baseline code paths.
 */
export interface RandomSource {
  next(): number;
}

export class MathRandomSource implements RandomSource {
  /** Returns the next non-deterministic random sample from `Math.random()`. */
  public next(): number {
    return Math.random();
  }
}

/**
 * Small deterministic PRNG for reproducible browser and worker runs.
 * This is not cryptographically secure, but it is fast and stable enough for
 * algorithm benchmarking and result reproduction.
 */
export function createSeededRandomSource(seed: number): RandomSource {
  let state = seed >>> 0;

  return {
    next(): number {
      state += 0x6d2b79f5;
      let t = state;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    },
  };
}
