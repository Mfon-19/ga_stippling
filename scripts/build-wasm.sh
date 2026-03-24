#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/cpp/build-wasm"

mkdir -p "${REPO_ROOT}/src/wasm/generated"

# Keep the WASM build in its own CMake tree so native and browser compilers do
# not stomp on each other's caches.
emcmake cmake -S "${REPO_ROOT}/cpp" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target stippling_engine_wasm
