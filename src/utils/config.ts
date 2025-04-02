export const CONFIG = {
  GENETIC: {
    DEFAULT_POPULATION_SIZE: 100,
    DEFAULT_MUTATION_RATE: 0.2,
    DEFAULT_DOT_COUNT: 50000,
    ELITISM_RATIO: 0.7,
  },
  IMAGE: {
    DEFAULT_BLUR: 0,
    DEFAULT_THRESHOLD: 130,
    MAX_DOT_COUNT: 200000,
  },
  ANIMATION: {
    FPS: 60,
  },
};

export const CANVAS_IDS = {
  MAIN: "canvas",
  IMAGE: "imgCanvas",
  BLACK_WHITE: "bwCanvas",
  EVOLUTION: "evolCanvas",
};
