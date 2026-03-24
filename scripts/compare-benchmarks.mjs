#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

const repoRoot = process.cwd();
const resultsDir = path.join(repoRoot, "benchmarks", "results");

function resolveReportPath(value) {
  return path.isAbsolute(value) ? value : path.join(repoRoot, value);
}

function loadReport(reportPath) {
  return JSON.parse(fs.readFileSync(reportPath, "utf8"));
}

function defaultReportPair() {
  const reports = fs
    .readdirSync(resultsDir)
    .filter((name) => name.startsWith("benchmark-") && name.endsWith(".json"))
    .sort();

  if (reports.length < 2) {
    throw new Error("Need at least two benchmark reports to compare");
  }

  return reports.slice(-2).map((name) => path.join(resultsDir, name));
}

const explicitReports = process.argv.slice(2);
const [baselinePath, candidatePath] =
  explicitReports.length >= 2
    ? explicitReports.map(resolveReportPath)
    : defaultReportPair();

const baseline = loadReport(baselinePath);
const candidate = loadReport(candidatePath);
const baselineByImage = new Map(
  baseline.images.map((image) => [image.image, image])
);

const comparison = candidate.images.map((image) => {
  const previous = baselineByImage.get(image.image);
  if (!previous) {
    return {
      image: image.image,
      status: "new",
    };
  }

  return {
    image: image.image,
    throughputDeltaPercent:
      ((image.wasm.generationsPerSecond - previous.wasm.generationsPerSecond) /
        previous.wasm.generationsPerSecond) *
      100,
    timeToQualityDeltaPercent:
      ((image.wasm.timeToTargetMs - previous.wasm.timeToTargetMs) /
        previous.wasm.timeToTargetMs) *
      100,
    mseDeltaPercent:
      ((image.wasm.quality.mse - previous.wasm.quality.mse) /
        previous.wasm.quality.mse) *
      100,
  };
});

console.log(
  JSON.stringify(
    {
      baseline: baselinePath,
      candidate: candidatePath,
      comparison,
    },
    null,
    2
  )
);
