import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

import { GeneticAlgorithm } from "../src/core/GeneticAlgorithm";
import { RasterImageProcessor } from "../src/shared/RasterImageProcessor";
import {
  EngineRunConfig,
  SerializedImageBuffer,
  TargetProcessingConfig,
} from "../src/shared/engineProtocol";
import { createSeededRandomSource } from "../src/shared/random";
import { CONFIG } from "../src/utils/config";
import { loadEngineModule } from "../src/wasm/engineModule";

class NodeImageData {
  public readonly data: Uint8ClampedArray;
  public readonly width: number;
  public readonly height: number;

  constructor(data: Uint8ClampedArray, width: number, height: number) {
    if (data.length !== width * height * 4) {
      throw new Error(
        `ImageData length ${data.length} does not match ${width}x${height}`
      );
    }

    this.data = data;
    this.width = width;
    this.height = height;
  }
}

type ImageDataLike = InstanceType<typeof NodeImageData>;

interface DecodedFixture {
  name: string;
  imagePath: string;
  imageData: ImageDataLike;
}

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

interface BackendRunResult {
  backend: "typescript" | "wasm";
  generations: number;
  elapsedMs: number;
  generationsPerSecond: number;
  finalFitness: number;
  history: GenerationSample[];
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
  typescript: BackendSummary;
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

const GLOBAL_SCOPE = globalThis as typeof globalThis & {
  ImageData?: typeof NodeImageData;
};

if (!GLOBAL_SCOPE.ImageData) {
  GLOBAL_SCOPE.ImageData = NodeImageData;
}

const REPO_ROOT = process.cwd();
const FIXTURE_NAMES = ["jobs.jpeg", "landscape.avif"];
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

function serializeImageData(imageData: ImageDataLike): SerializedImageBuffer {
  return {
    width: imageData.width,
    height: imageData.height,
    format: "rgba8",
    pixels: imageData.data.slice().buffer,
  };
}

function decodeFixtureImage(imagePath: string): DecodedFixture {
  const tempDirectory = mkdtempSync(path.join(tmpdir(), "stippling-benchmark-"));
  const outputPath = path.join(tempDirectory, `${path.basename(imagePath)}.rgba`);

  try {
    const stdout = execFileSync(
      "swift",
      [path.join(REPO_ROOT, "scripts", "decode-image.swift"), imagePath, outputPath],
      {
        cwd: REPO_ROOT,
        encoding: "utf8",
      }
    );
    const payload = JSON.parse(stdout) as {
      width: number;
      height: number;
      output: string;
    };
    const pixels = Uint8ClampedArray.from(readFileSync(payload.output));

    return {
      name: path.basename(imagePath),
      imagePath,
      imageData: new NodeImageData(pixels, payload.width, payload.height),
    };
  } finally {
    rmSync(tempDirectory, { recursive: true, force: true });
  }
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

  return {
    backend: "typescript",
    generations,
    elapsedMs,
    generationsPerSecond: elapsedMs > 0 ? generations / (elapsedMs / 1000) : 0,
    finalFitness: history[history.length - 1]?.bestFitness ?? 0,
    history,
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

    return {
      backend: "wasm",
      generations,
      elapsedMs,
      generationsPerSecond: elapsedMs > 0 ? generations / (elapsedMs / 1000) : 0,
      finalFitness: history[history.length - 1]?.bestFitness ?? 0,
      history,
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
    typescript: summarizedTypescript,
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
        `  TS: ${result.typescript.generationsPerSecond.toFixed(2)} gen/s`,
        `timeToTarget=${result.typescript.timeToTargetMs.toFixed(1)} ms`,
        `targetGen=${result.typescript.generationAtTarget}`,
        `finalFitness=${result.typescript.finalFitness.toFixed(4)}`,
      ].join(" | ")
    );
    console.log(
      [
        `  WASM: ${result.wasm.generationsPerSecond.toFixed(2)} gen/s`,
        `timeToTarget=${result.wasm.timeToTargetMs.toFixed(1)} ms`,
        `targetGen=${result.wasm.generationAtTarget}`,
        `finalFitness=${result.wasm.finalFitness.toFixed(4)}`,
      ].join(" | ")
    );
    console.log(
      `  Speedup: ${result.throughputSpeedupX.toFixed(2)}x throughput, ${result.timeToQualityFasterPercent.toFixed(1)}% faster to same quality`
    );
  });
}

async function main(): Promise<void> {
  const fixturePaths = FIXTURE_NAMES.map((name) => path.join(REPO_ROOT, name));
  const fixtures = fixturePaths.map(decodeFixtureImage).map(prepareTargetFixture);
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
