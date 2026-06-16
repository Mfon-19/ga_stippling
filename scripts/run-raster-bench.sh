#!/usr/bin/env bash
set -euo pipefail

# Builds and runs the native raster fitness microbenchmark, which isolates the
# incremental-vs-full-redraw algorithmic win inside a single optimized binary
# (no C++ vs. JavaScript language gap). Results are written locally under
# benchmarks/raster/ as CSV and JSON.

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/cpp/build-bench"
OUTPUT_DIR="$ROOT_DIR/benchmarks/raster"

mkdir -p "$OUTPUT_DIR"

cmake -S "$ROOT_DIR/cpp" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --target stippling_raster_bench >/dev/null

"$BUILD_DIR/stippling_raster_bench" \
  --csv "$OUTPUT_DIR/raster-bench.csv" \
  --json "$OUTPUT_DIR/raster-bench.json" \
  "$@"
