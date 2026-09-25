export const CONFIG = {
  GENETIC: {
    DEFAULT_POPULATION_SIZE: 100,
    DEFAULT_MUTATION_RATE: 0.2,
    ELITISM_RATIO: 0.15,
  },
  IMAGE: {
    DEFAULT_BLUR: 0,
    DEFAULT_THRESHOLD: 130,
    MAX_DOT_COUNT: 200000,
    // Uploads are downscaled to this longest edge: every candidate keeps a
    // full-size raster, so memory grows with population x pixels.
    MAX_DIMENSION: 1024,
  },
  RUN: {
    PREVIEW_INTERVAL_MS: 100,
    // Slider changes re-prepare the target only once input settles.
    PROCESSING_DEBOUNCE_MS: 150,
    EXPORT_SCALE: 4,
    TIMELAPSE_FRAME_DURATION_MS: 120,
  },
};

export const CANVAS_IDS = {
  IMAGE: "imgCanvas",
  BLACK_WHITE: "bwCanvas",
  EVOLUTION: "evolCanvas",
};
