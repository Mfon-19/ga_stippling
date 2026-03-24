import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

import { EngineRunConfig, SerializedImageBuffer, TargetProcessingConfig } from "../src/shared/engineProtocol";
import { CONFIG } from "../src/utils/config";
import { loadEngineModule } from "../src/wasm/engineModule";
import { decodeImageFile, installNodeImageData } from "./imageDecode";

interface CliRunReport {
  input: string;
  image: { width: number; height: number };
  config: { dotCount: number };
  progress: { generation: number; bestFitness: number; bestSquaredError: number };
  validation: { valid: boolean };
  bestDots?: Array<{ x: number; y: number; radius: number }>;
}

interface ParityImageResult {
  image: string;
  dotCount: number;
  passed: boolean;
  svgMatched: boolean;
  pngMatched: boolean;
  dotsMatched: boolean;
  fitnessDelta: number;
}

interface ParityReport {
  generatedAt: string;
  config: {
    generations: number;
    populationSize: number;
    mutationRate: number;
    elitismRatio: number;
    seed: number;
    scale: number;
  };
  images: ParityImageResult[];
}

installNodeImageData();

const REPO_ROOT = process.cwd();
const PROCESSING_CONFIG: TargetProcessingConfig = {
  blurAmount: CONFIG.IMAGE.DEFAULT_BLUR,
  threshold: CONFIG.IMAGE.DEFAULT_THRESHOLD,
  maxDotCount: CONFIG.IMAGE.MAX_DOT_COUNT,
};
const FIXTURE_PATHS = [
  path.join(REPO_ROOT, "fixtures", "regression", "cross.pgm"),
  path.join(REPO_ROOT, "fixtures", "regression", "bands.ppm"),
];
const RUN_CONFIG_BASE = {
  populationSize: 24,
  mutationRate: 0.2,
  elitismRatio: 0.15,
  seed: 20260324,
  generationsPerBatch: 1,
  previewIntervalMs: 100,
  benchmarkMode: true,
} satisfies Omit<EngineRunConfig, "dotCount">;
const GENERATIONS = 5;
const EXPORT_SCALE = 3;

function serializeImageData(imageData: ImageData): SerializedImageBuffer {
  return {
    width: imageData.width,
    height: imageData.height,
    format: "rgba8",
    pixels: new Uint8ClampedArray(imageData.data).buffer,
  };
}

function hashBuffer(buffer: ArrayBuffer | Uint8Array): string {
  const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  return createHash("sha256").update(bytes).digest("hex");
}

function dotsMatch(
  left: Array<{ x: number; y: number; radius: number }>,
  right: Array<{ x: number; y: number; radius: number }>
): boolean {
  if (left.length !== right.length) {
    return false;
  }

  return left.every((dot, index) => {
    const other = right[index];
    return (
      Math.abs(dot.x - other.x) <= 1e-6 &&
      Math.abs(dot.y - other.y) <= 1e-6 &&
      Math.abs(dot.radius - other.radius) <= 1e-6
    );
  });
}

async function runParityForImage(imagePath: string): Promise<ParityImageResult> {
  const decoded = decodeImageFile(imagePath);
  const module = await loadEngineModule();
  const engine = module.createEngine();
  const tempDirectory = mkdtempSync(path.join(tmpdir(), "stippling-parity-"));

  try {
    const prepared = engine.prepareTarget(
      serializeImageData(decoded.imageData),
      PROCESSING_CONFIG
    );
    const dotCount = prepared.stats.recommendedDotCount;
    const runConfig: EngineRunConfig = {
      ...RUN_CONFIG_BASE,
      dotCount,
    };

    engine.configure(runConfig);
    engine.initializeOptimizer();
    let wasmBestFitness = 0;
    for (let generation = 0; generation < GENERATIONS; generation += 1) {
      wasmBestFitness = engine.evolveBatch().bestFitness;
    }

    const wasmDots = engine.getBestDots();
    const wasmSvg = engine.exportBestSvg(EXPORT_SCALE);
    const wasmPng = engine.exportBestPng(EXPORT_SCALE);

    const svgPath = path.join(tempDirectory, "native.svg");
    const pngPath = path.join(tempDirectory, "native.png");
    const cliOutput = execFileSync(
      path.join(REPO_ROOT, "cpp", "build", "stippling_cli"),
      [
        "run",
        "--input",
        imagePath,
        "--generations",
        String(GENERATIONS),
        "--population",
        String(runConfig.populationSize),
        "--mutation",
        String(runConfig.mutationRate),
        "--elitism",
        String(runConfig.elitismRatio),
        "--dot-count",
        String(dotCount),
        "--seed",
        String(runConfig.seed),
        "--scale",
        String(EXPORT_SCALE),
        "--validate",
        "--include-dots",
        "--svg",
        svgPath,
        "--png",
        pngPath,
        "--report",
        "-",
      ],
      {
        cwd: REPO_ROOT,
        encoding: "utf8",
      }
    );
    const cliReport = JSON.parse(cliOutput) as CliRunReport;
    const cliSvg = readFileSync(svgPath, "utf8");
    const cliPng = readFileSync(pngPath);

    const svgMatched = cliSvg === wasmSvg;
    const pngMatched = hashBuffer(cliPng) === hashBuffer(wasmPng);
    const dotsMatched = dotsMatch(cliReport.bestDots ?? [], wasmDots);
    const fitnessDelta = Math.abs(cliReport.progress.bestFitness - wasmBestFitness);

    const passed =
      cliReport.validation.valid &&
      cliReport.progress.generation === GENERATIONS &&
      fitnessDelta <= 1e-9 &&
      svgMatched &&
      pngMatched &&
      dotsMatched;

    return {
      image: decoded.name,
      dotCount,
      passed,
      svgMatched,
      pngMatched,
      dotsMatched,
      fitnessDelta,
    };
  } finally {
    engine.dispose();
    module.dispose();
    rmSync(tempDirectory, { recursive: true, force: true });
  }
}

async function main(): Promise<void> {
  const fixturePaths =
    process.argv.length > 2
      ? process.argv.slice(2).map((fixturePath) =>
          path.isAbsolute(fixturePath)
            ? fixturePath
            : path.join(REPO_ROOT, fixturePath)
        )
      : FIXTURE_PATHS;
  const results: ParityImageResult[] = [];

  for (const fixturePath of fixturePaths) {
    results.push(await runParityForImage(fixturePath));
  }

  const report: ParityReport = {
    generatedAt: new Date().toISOString(),
    config: {
      generations: GENERATIONS,
      populationSize: RUN_CONFIG_BASE.populationSize,
      mutationRate: RUN_CONFIG_BASE.mutationRate,
      elitismRatio: RUN_CONFIG_BASE.elitismRatio,
      seed: RUN_CONFIG_BASE.seed,
      scale: EXPORT_SCALE,
    },
    images: results,
  };
  const resultsDirectory = path.join(REPO_ROOT, "benchmarks", "results");
  mkdirSync(resultsDirectory, { recursive: true });
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const reportPath = path.join(resultsDirectory, `parity-${timestamp}.json`);
  writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, "utf8");

  console.log(`Parity report: ${reportPath}`);
  for (const result of results) {
    console.log(
      `${result.image}: ${result.passed ? "PASS" : "FAIL"} | dots=${result.dotCount} | svg=${result.svgMatched} | png=${result.pngMatched} | dotsMatch=${result.dotsMatched} | fitnessDelta=${result.fitnessDelta}`
    );
  }

  if (results.some((result) => !result.passed)) {
    process.exitCode = 1;
  }
}

void main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
