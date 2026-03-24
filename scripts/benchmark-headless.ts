import {
  existsSync,
  mkdirSync,
  readdirSync,
  statSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";

import { GeneticAlgorithm } from "../archive/typescript-ga/src/core/GeneticAlgorithm";
import { RasterImageProcessor } from "../src/shared/RasterImageProcessor";
import {
  EngineRunConfig,
  SerializedDot,
  SerializedImageBuffer,
  TargetProcessingConfig,
} from "../src/shared/engineProtocol";
import { createSeededRandomSource } from "../src/shared/random";
import { CONFIG } from "../src/utils/config";
import { loadEngineModule } from "../src/wasm/engineModule";
import {
  DecodedFixture,
  NodeImageData,
  decodeImageFile,
  installNodeImageData,
} from "./imageDecode";

type ImageDataLike = NodeImageData;

interface TargetFixture {
  name: string;
  imagePath: string;
  sourceWidth: number;
  sourceHeight: number;
  processedImage: ImageDataLike;
  processedBuffer: SerializedImageBuffer;
  dotCount: number;
  blackPixels: number;
  blackPercentage: number;
}

interface GenerationSample {
  generation: number;
  elapsedMs: number;
  bestFitness: number;
}

interface QualityMetrics {
  mse: number;
  rmse: number;
  psnr: number;
  exactPixelRatio: number;
}

interface BackendRunResult {
  backend: "typescript-archive" | "wasm";
  generations: number;
  elapsedMs: number;
  generationsPerSecond: number;
  finalFitness: number;
  history: GenerationSample[];
  bestDots: SerializedDot[];
  quality: QualityMetrics;
  usedHeapBytes?: number;
}

interface BenchmarkResult {
  image: string;
  dimensions: string;
  dotCount: number;
  blackPixels: number;
  blackPercentage: number;
  measuredGenerations: number;
  targetFitness: number;
  typescriptArchive: BackendSummary;
  wasm: BackendSummary;
  throughputSpeedupX: number;
  timeToQualityFasterPercent: number;
}

interface BackendSummary {
  generationsPerSecond: number;
  elapsedMs: number;
  finalFitness: number;
  timeToTargetMs: number;
  generationAtTarget: number;
  quality: QualityMetrics;
  usedHeapBytes?: number;
}

interface JsonReport {
  generatedAt: string;
  seed: number;
  repeatCount: number;
  processing: TargetProcessingConfig;
  runDefaults: Omit<EngineRunConfig, "dotCount" | "seed">;
  images: BenchmarkResult[];
}

installNodeImageData();

const REPO_ROOT = process.cwd();
const BENCHMARK_SEED = 1337;
const PROCESSING_CONFIG: TargetProcessingConfig = {
  blurAmount: CONFIG.IMAGE.DEFAULT_BLUR,
  threshold: CONFIG.IMAGE.DEFAULT_THRESHOLD,
  maxDotCount: CONFIG.IMAGE.MAX_DOT_COUNT,
};
const REPEAT_COUNT = 5;
const IDENTITY_PROCESSING_CONFIG: TargetProcessingConfig = {
  blurAmount: 0,
  threshold: CONFIG.IMAGE.DEFAULT_THRESHOLD,
  maxDotCount: CONFIG.IMAGE.MAX_DOT_COUNT,
};
const REGRESSION_FIXTURE_DIRECTORY = path.join(
  REPO_ROOT,
  "fixtures",
  "regression"
);
const NETPBM_EXTENSIONS = new Set([".pgm", ".ppm", ".pnm"]);

function collectFixtureFiles(directory: string): string[] {
  return readdirSync(directory)
    .filter((entry) =>
      NETPBM_EXTENSIONS.has(path.extname(entry).toLowerCase())
    )
    .sort()
    .map((entry) => path.join(directory, entry));
}

function expandFixtureInputs(argumentsList: string[]): string[] {
  return argumentsList.flatMap((fixturePath) => {
    const resolvedPath = path.isAbsolute(fixturePath)
      ? fixturePath
      : path.join(REPO_ROOT, fixturePath);
    return statSync(resolvedPath).isDirectory()
      ? collectFixtureFiles(resolvedPath)
      : [resolvedPath];
  });
}

function serializeImageData(imageData: ImageDataLike): SerializedImageBuffer {
  return {
    width: imageData.width,
    height: imageData.height,
    format: "rgba8",
    pixels: imageData.data.slice().buffer,
  };
}

function drawHorizontalLine(
  grid: Uint8Array,
  width: number,
  height: number,
  y: number,
  startX: number,
  endX: number
): void {
  if (y < 0 || y >= height) {
    return;
  }

  const clampedStart = Math.max(0, startX);
  const clampedEnd = Math.min(width - 1, endX);

  for (let x = clampedStart; x <= clampedEnd; x += 1) {
    grid[y * width + x] = 0;
  }
}

function renderDotsToGrayscale(
  dots: SerializedDot[],
  width: number,
  height: number
): Uint8Array {
  const grid = new Uint8Array(width * height).fill(255);

  for (const dot of dots) {
    const centerX = Math.floor(dot.x);
    const centerY = Math.floor(dot.y);
    const radius = Math.floor(dot.radius);

    if (radius < 0) {
      continue;
    }

    let x = 0;
    let y = radius;
    let decision = 1 - radius;

    drawHorizontalLine(grid, width, height, centerY, centerX - radius, centerX + radius);

    while (y > x) {
      if (decision < 0) {
        decision += 2 * x + 3;
      } else {
        decision += 2 * (x - y) + 5;
        y -= 1;
      }
      x += 1;

      drawHorizontalLine(grid, width, height, centerY + y, centerX - x, centerX + x);
      drawHorizontalLine(grid, width, height, centerY - y, centerX - x, centerX + x);
      drawHorizontalLine(grid, width, height, centerY + x, centerX - y, centerX + y);
      drawHorizontalLine(grid, width, height, centerY - x, centerX - y, centerX + y);
    }
  }

  return grid;
}

function computeQualityMetrics(
  targetImage: ImageDataLike,
  bestDots: SerializedDot[]
): QualityMetrics {
  const rendered = renderDotsToGrayscale(bestDots, targetImage.width, targetImage.height);
  let squaredError = 0;
  let exactMatches = 0;

  for (let index = 0; index < rendered.length; index += 1) {
    const target = targetImage.data[index * 4];
    const diff = rendered[index] - target;
    squaredError += diff * diff;
    if (rendered[index] === target) {
      exactMatches += 1;
    }
  }

  const mse = squaredError / rendered.length;
  const rmse = Math.sqrt(mse);
  const psnr =
    mse === 0 ? Number.POSITIVE_INFINITY : 20 * Math.log10(255) - 10 * Math.log10(mse);

  return {
    mse,
    rmse,
    psnr,
    exactPixelRatio: exactMatches / rendered.length,
  };
}

function prepareTargetFixture(decodedFixture: DecodedFixture): TargetFixture {
  const processor = new RasterImageProcessor();
  const processed = processor.preprocess(decodedFixture.imageData, PROCESSING_CONFIG);

  return {
    name: decodedFixture.name,
    imagePath: decodedFixture.imagePath,
    sourceWidth: decodedFixture.imageData.width,
    sourceHeight: decodedFixture.imageData.height,
    processedImage: processed.imageData as ImageDataLike,
    processedBuffer: serializeImageData(processed.imageData as ImageDataLike),
    dotCount: processed.stats.recommendedDotCount,
    blackPixels: processed.stats.blackPixels,
    blackPercentage: processed.stats.blackPercentage,
  };
}

function createRunConfig(dotCount: number): EngineRunConfig {
  return {
    populationSize: CONFIG.GENETIC.DEFAULT_POPULATION_SIZE,
    mutationRate: CONFIG.GENETIC.DEFAULT_MUTATION_RATE,
    dotCount,
    elitismRatio: CONFIG.GENETIC.ELITISM_RATIO,
    seed: BENCHMARK_SEED,
    generationsPerBatch: 1,
    previewIntervalMs: 100,
    benchmarkMode: true,
  };
}

function getBestFitness(algorithm: GeneticAlgorithm): number {
  const population = algorithm.getPopulation();
  const fittestIndex = population.getFittestIndex();
  return population.population[fittestIndex].fitness;
}

function estimateMeasurementGenerations(firstGenerationMs: number): number {
  if (firstGenerationMs <= 0) {
    return 6;
  }

  const targetDurationMs = 2500;
  return Math.max(5, Math.min(10, Math.round(targetDurationMs / firstGenerationMs)));
}

function summarizeBackendRun(
  runs: BackendRunResult[],
  targetFitness: number
): BackendSummary {
  if (runs.length === 0) {
    throw new Error("At least one backend run is required for summarization");
  }

  const summaryInputs = runs.map((run) => {
    const targetSample =
      run.history.find((sample) => sample.bestFitness >= targetFitness) ??
      run.history[run.history.length - 1];

    return {
      generationsPerSecond: run.generationsPerSecond,
      elapsedMs: run.elapsedMs,
      finalFitness: run.finalFitness,
      timeToTargetMs: targetSample.elapsedMs,
      generationAtTarget: targetSample.generation,
      quality: run.quality,
      usedHeapBytes: run.usedHeapBytes,
    };
  });

  return {
    generationsPerSecond: median(
      summaryInputs.map((input) => input.generationsPerSecond)
    ),
    elapsedMs: median(summaryInputs.map((input) => input.elapsedMs)),
    finalFitness: median(summaryInputs.map((input) => input.finalFitness)),
    timeToTargetMs: median(
      summaryInputs.map((input) => input.timeToTargetMs)
    ),
    generationAtTarget: medianInteger(
      summaryInputs.map((input) => input.generationAtTarget)
    ),
    quality: {
      mse: median(summaryInputs.map((input) => input.quality.mse)),
      rmse: median(summaryInputs.map((input) => input.quality.rmse)),
      psnr: median(summaryInputs.map((input) => input.quality.psnr)),
      exactPixelRatio: median(
        summaryInputs.map((input) => input.quality.exactPixelRatio)
      ),
    },
    usedHeapBytes: medianOptionalInteger(
      summaryInputs.map((input) => input.usedHeapBytes)
    ),
  };
}

function median(values: number[]): number {
  if (values.length === 0) {
    throw new Error("Median requires at least one value");
  }

  const sorted = [...values].sort((left, right) => left - right);
  const middle = Math.floor(sorted.length / 2);

  if (sorted.length % 2 === 1) {
    return sorted[middle];
  }

  return (sorted[middle - 1] + sorted[middle]) / 2;
}

function medianInteger(values: number[]): number {
  return Math.round(median(values));
}

function medianOptionalInteger(values: Array<number | undefined>): number | undefined {
  const presentValues = values.filter(
    (value): value is number => typeof value === "number"
  );

  if (presentValues.length === 0) {
    return undefined;
  }

  return medianInteger(presentValues);
}

function runTypescriptGenerations(
  fixture: TargetFixture,
  config: EngineRunConfig,
  generations: number
): BackendRunResult {
  const algorithm = new GeneticAlgorithm(fixture.processedImage, {
    populationSize: config.populationSize,
    mutationRate: config.mutationRate,
    dotCount: config.dotCount,
    elitismRatio: config.elitismRatio,
    random: createSeededRandomSource(config.seed),
  });
  const history: GenerationSample[] = [];
  const startedAt = performance.now();

  for (let generation = 1; generation <= generations; generation += 1) {
    algorithm.evolve();
    history.push({
      generation,
      elapsedMs: performance.now() - startedAt,
      bestFitness: getBestFitness(algorithm),
    });
  }

  const elapsedMs = history[history.length - 1]?.elapsedMs ?? 0;
  const population = algorithm.getPopulation();
  const fittestIndex = population.getFittestIndex();
  const bestDots = population.population[fittestIndex].dots.map((dot) => ({
    x: dot.x,
    y: dot.y,
    radius: dot.radius,
  }));

  return {
    backend: "typescript-archive",
    generations,
    elapsedMs,
    generationsPerSecond: elapsedMs > 0 ? generations / (elapsedMs / 1000) : 0,
    finalFitness: history[history.length - 1]?.bestFitness ?? 0,
    history,
    bestDots,
    quality: computeQualityMetrics(fixture.processedImage, bestDots),
  };
}

async function runWasmGenerations(
  fixture: TargetFixture,
  config: EngineRunConfig,
  generations: number
): Promise<BackendRunResult> {
  const module = await loadEngineModule();
  const engine = module.createEngine();

  try {
    engine.prepareTarget(
      {
        width: fixture.processedBuffer.width,
        height: fixture.processedBuffer.height,
        format: "rgba8",
        pixels: fixture.processedBuffer.pixels.slice(0),
      },
      IDENTITY_PROCESSING_CONFIG
    );
    engine.configure(config);
    engine.initializeOptimizer();

    const history: GenerationSample[] = [];
    const startedAt = performance.now();

    for (let generation = 1; generation <= generations; generation += 1) {
      const progress = engine.evolveBatch();
      history.push({
        generation,
        elapsedMs: performance.now() - startedAt,
        bestFitness: progress.bestFitness,
      });
    }

    const elapsedMs = history[history.length - 1]?.elapsedMs ?? 0;
    const bestDots = engine.getBestDots();

    return {
      backend: "wasm",
      generations,
      elapsedMs,
      generationsPerSecond: elapsedMs > 0 ? generations / (elapsedMs / 1000) : 0,
      finalFitness: history[history.length - 1]?.bestFitness ?? 0,
      history,
      bestDots,
      quality: computeQualityMetrics(fixture.processedImage, bestDots),
      usedHeapBytes: engine.heapByteLength(),
    };
  } finally {
    engine.dispose();
    module.dispose();
  }
}

async function benchmarkFixture(fixture: TargetFixture): Promise<BenchmarkResult> {
  const runConfig = createRunConfig(fixture.dotCount);
  const firstGenerationRun = runTypescriptGenerations(fixture, runConfig, 1);
  const measuredGenerations = estimateMeasurementGenerations(firstGenerationRun.elapsedMs);
  const warmupTypescriptRun = runTypescriptGenerations(
    fixture,
    runConfig,
    measuredGenerations
  );
  const warmupWasmRun = await runWasmGenerations(fixture, runConfig, measuredGenerations);
  const typescriptRuns: BackendRunResult[] = [];
  const wasmRuns: BackendRunResult[] = [];

  for (let iteration = 0; iteration < REPEAT_COUNT; iteration += 1) {
    typescriptRuns.push(
      runTypescriptGenerations(fixture, runConfig, measuredGenerations)
    );
    wasmRuns.push(await runWasmGenerations(fixture, runConfig, measuredGenerations));
  }

  const targetFitness = Math.min(
    warmupTypescriptRun.finalFitness,
    warmupWasmRun.finalFitness
  );
  const summarizedTypescript = summarizeBackendRun(typescriptRuns, targetFitness);
  const summarizedWasm = summarizeBackendRun(wasmRuns, targetFitness);

  return {
    image: fixture.name,
    dimensions: `${fixture.sourceWidth}x${fixture.sourceHeight}`,
    dotCount: fixture.dotCount,
    blackPixels: fixture.blackPixels,
    blackPercentage: fixture.blackPercentage,
    measuredGenerations,
    targetFitness,
    typescriptArchive: summarizedTypescript,
    wasm: summarizedWasm,
    throughputSpeedupX:
      summarizedTypescript.generationsPerSecond > 0
        ? summarizedWasm.generationsPerSecond /
          summarizedTypescript.generationsPerSecond
        : 0,
    timeToQualityFasterPercent:
      summarizedTypescript.timeToTargetMs > 0
        ? ((summarizedTypescript.timeToTargetMs - summarizedWasm.timeToTargetMs) /
            summarizedTypescript.timeToTargetMs) *
          100
        : 0,
  };
}

function writeReport(report: JsonReport): string {
  const resultsDirectory = path.join(REPO_ROOT, "benchmarks", "results");
  mkdirSync(resultsDirectory, { recursive: true });

  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const reportPath = path.join(resultsDirectory, `benchmark-${timestamp}.json`);

  writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, "utf8");
  return reportPath;
}

function printReport(reportPath: string, report: JsonReport): void {
  console.log(`Benchmark report: ${reportPath}`);

  report.images.forEach((result) => {
    console.log(
      [
        `${result.image} (${result.dimensions})`,
        `dots=${result.dotCount}`,
        `measuredGenerations=${result.measuredGenerations}`,
        `targetFitness=${result.targetFitness.toFixed(4)}`,
      ].join(" | ")
    );
    console.log(
      [
        `  Archived TS: ${result.typescriptArchive.generationsPerSecond.toFixed(2)} gen/s`,
        `timeToTarget=${result.typescriptArchive.timeToTargetMs.toFixed(1)} ms`,
        `targetGen=${result.typescriptArchive.generationAtTarget}`,
        `finalFitness=${result.typescriptArchive.finalFitness.toFixed(4)}`,
        `mse=${result.typescriptArchive.quality.mse.toFixed(1)}`,
      ].join(" | ")
    );
    console.log(
      [
        `  WASM: ${result.wasm.generationsPerSecond.toFixed(2)} gen/s`,
        `timeToTarget=${result.wasm.timeToTargetMs.toFixed(1)} ms`,
        `targetGen=${result.wasm.generationAtTarget}`,
        `finalFitness=${result.wasm.finalFitness.toFixed(4)}`,
        `mse=${result.wasm.quality.mse.toFixed(1)}`,
      ].join(" | ")
    );
    console.log(
      `  Speedup: ${result.throughputSpeedupX.toFixed(2)}x throughput, ${result.timeToQualityFasterPercent.toFixed(1)}% faster to same quality`
    );
  });
}

async function main(): Promise<void> {
  const explicitFixtures = process.argv.slice(2);
  const defaultFixtures = ["jobs.jpeg", "landscape.avif"]
    .map((name) => path.join(REPO_ROOT, name))
    .filter((candidate) => existsSync(candidate));
  const fallbackFixtures = collectFixtureFiles(REGRESSION_FIXTURE_DIRECTORY);
  const fixturePaths =
    explicitFixtures.length > 0
      ? expandFixtureInputs(explicitFixtures)
      : defaultFixtures.length > 0
        ? defaultFixtures
        : fallbackFixtures;
  const fixtures = fixturePaths.map(decodeImageFile).map(prepareTargetFixture);
  const results: BenchmarkResult[] = [];

  for (const fixture of fixtures) {
    results.push(await benchmarkFixture(fixture));
  }

  const report: JsonReport = {
    generatedAt: new Date().toISOString(),
    seed: BENCHMARK_SEED,
    repeatCount: REPEAT_COUNT,
    processing: PROCESSING_CONFIG,
    runDefaults: {
      populationSize: CONFIG.GENETIC.DEFAULT_POPULATION_SIZE,
      mutationRate: CONFIG.GENETIC.DEFAULT_MUTATION_RATE,
      elitismRatio: CONFIG.GENETIC.ELITISM_RATIO,
      generationsPerBatch: 1,
      previewIntervalMs: 100,
      benchmarkMode: true,
    },
    images: results,
  };
  const reportPath = writeReport(report);
  printReport(reportPath, report);
}

void main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
