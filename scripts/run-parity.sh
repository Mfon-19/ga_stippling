#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$ROOT_DIR/.tmp"
OUTPUT_FILE="$OUTPUT_DIR/parity-check.mjs"

mkdir -p "$OUTPUT_DIR"

"$ROOT_DIR/node_modules/.bin/esbuild" \
  "$ROOT_DIR/scripts/parity-check.ts" \
  --bundle \
  --platform=node \
  --format=esm \
  --target=node22 \
  --outfile="$OUTPUT_FILE"

node "$OUTPUT_FILE" "$@"
