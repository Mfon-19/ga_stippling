# C++/WASM Stippling Engine Migration Plan

## Goal

Turn the current browser-only TypeScript stippling prototype into a more serious engineering project by:

- moving compute-heavy work into a C++ core
- compiling that core to WebAssembly for the browser
- running the optimizer inside a Web Worker
- improving the algorithm and data layout instead of just porting the current implementation line-for-line
- reusing the same C++ engine for a native CLI, benchmarks, and export workflows

The objective is not "rewrite everything in C++." The objective is to keep the browser app thin and move the right responsibilities into a worker-hosted C++ engine.

## Target Architecture

### High-level split

- TypeScript owns UI, file input, canvas presentation, and browser integration.
- A dedicated Web Worker owns long-running optimization work.
- The optimization engine, preprocessing, rasterization, fitness evaluation, and multiscale logic live in C++.
- The C++ core is compiled to WebAssembly for the browser and also built as a native CLI for batch and benchmark workflows.

### Browser responsibilities

- upload and validate images
- display original, processed, and best-current outputs
- send configuration to the worker
- throttle preview rendering and progress updates
- expose export controls and benchmark views

### Worker responsibilities

- initialize the WASM module
- load image/configuration data into the engine
- run generations without blocking the UI thread
- stream progress, metrics, and snapshots back to the UI
- execute exports that depend on engine state

### C++ engine responsibilities

- preprocessing and importance-map generation
- target pyramid creation for multiscale optimization
- population storage and mutation/crossover logic
- incremental raster/fitness bookkeeping
- deterministic random number generation
- snapshot serialization for export, timelapse, and regression testing

## Repository Layout

```text
/Users/mfonudoh/Desktop/Programs/stippling
├── cpp/
│   ├── engine/        # Shared C++ core for WASM + native CLI
│   ├── cli/           # Native batch runner / exporters / benchmark entrypoint
│   └── tests/         # C++ unit and regression tests
├── docs/              # Design docs, migration notes, benchmark reports
├── src/
│   ├── ui/            # Browser controls and canvas rendering
│   ├── worker/        # Worker entrypoint and message handling
│   └── wasm/          # TS wrapper around the generated WASM module
├── benchmarks/        # Benchmark inputs, configs, and generated reports
├── fixtures/          # Sample inputs and golden outputs
└── .github/workflows/ # CI for TS, WASM, native CLI, and benchmarks
```

## Current Progress

### Completed Sprints

1. Cleanup and baseline scaffolding
   - README drift fixed
   - obvious dead code removed from the current TypeScript app
   - `docs/`, `benchmarks/`, and `fixtures/` scaffolding added
   - migration plan documented in this file
2. Worker/WASM boundary scaffold
   - typed worker protocol added
   - worker entrypoint added
   - browser client wrapper added
   - app startup now boots a worker-backed engine boundary
3. Native C++ scaffold
   - initial `cpp/` tree added
   - CMake build added
   - shared native engine surface added
   - native CLI stub added
   - native smoke test added
4. Worker-owned target preparation
   - preprocessing logic moved into a shared pure-data processor
   - worker backend now prepares target rasters and image statistics
   - UI now uses the worker path for preprocessing when available
   - old canvas-bound preprocessing helper removed
5. Deterministic runs and progress metrics
   - seeded randomness added to the TypeScript GA path
   - worker progress events now include timing and throughput metrics
   - UI now surfaces seed, fitness, and generation speed
   - default benchmark run configuration added under `benchmarks/`
6. Native preprocessing surface
   - C++ engine now owns target-processing configuration and target statistics
   - native preprocessing path can convert rgba input into a thresholded target image
   - CLI and smoke test now exercise target preparation instead of only config storage
7. Incremental raster groundwork
   - native dot and raster-grid primitives added
   - overlap-aware coverage tracking added for binary stipple rasters
   - incremental dot updates now have correctness tests against full redraws
8. Native C ABI surface
   - plain C ABI added around native engine config and target preparation
   - ABI is now testable without directly depending on the C++ classes
   - this creates a WASM-friendly boundary once an Emscripten toolchain is available
9. Native optimizer loop
   - deterministic native optimizer added on top of the shared engine surface
   - engine can now initialize a native population and step batches of generations
   - optimizer determinism is covered by a native test
10. Native ABI and CLI optimizer stepping
   - C ABI now exposes optimizer initialization and batch stepping
   - native C API test now exercises optimizer progress, not only target preparation
   - CLI now prints optimizer progress metrics after stepping a generation batch

### Current State

- The browser app now routes target preparation and optimization through the worker-backed TypeScript backend when the worker is available.
- The native C++ engine now has a real target-preparation API and incremental raster foundations, but it is still not a feature-parity optimizer implementation.
- A plain C ABI now exists for the native engine, which is the right integration boundary for a future WASM build.
- The native core now has a deterministic optimizer loop, but it still uses full redraw fitness rather than the future incremental path.
- The native CLI and C ABI can now report optimizer progress, which is enough to support the first browser-side WASM adapter once the toolchain is installed.
- Code comments have been added to the worker boundary and C++ scaffold, and that standard needs to continue through every subsequent sprint.
- Preprocessing has been detached from DOM canvas contexts and moved into a shared pixel pipeline, which is a prerequisite for the native port.

### Remaining High-Priority Work

1. Replace the worker TypeScript backend with a real C++/WASM adapter.
2. Install or provision the Emscripten toolchain so the new C ABI can be compiled and wired into the browser worker.
3. Compile the native engine and C ABI to WASM and replace the worker TypeScript backend with that adapter.
4. Replace full redraw fitness in the native optimizer with true incremental fitness on the raster-grid foundation.
5. Expand deterministic benchmarking into saved reports, comparison tooling, and native/browser parity checks.
6. Add multiscale optimization and stronger search heuristics.
7. Expand exports, CLI workflows, tests, and CI.

## Delivery Phases

### Phase 1: Cleanup and Baseline

Status: partially complete

Purpose: make the repo look intentional before major migration work begins.

Deliverables:

- fix README drift, including references to folders that do not exist
- remove dead code from the current TypeScript app
- define a versioned configuration schema for runs
- add a small set of benchmark fixture images
- capture baseline metrics from the current TypeScript implementation

Notes:

- This phase should establish the "before" state for performance and quality.
- The current hotspots are the full fitness recomputation in `src/core/Population.ts` and the main-thread animation loop in `src/ui/EventHandlers.ts`.

### Phase 2: Worker Protocol and WASM Build Skeleton

Status: partially complete

Purpose: define stable integration boundaries before the heavy port begins.

Deliverables:

- create a worker message protocol with messages such as `init`, `loadImage`, `start`, `pause`, `resume`, `step`, `snapshot`, `exportSvg`, `exportPng`, and `benchmark`
- set up a CMake-based C++ project
- set up Emscripten build output for the browser
- add a minimal TypeScript wrapper for loading the WASM module
- prove the worker can initialize the engine and round-trip simple data

Important design rule:

- Keep the JS/WASM boundary coarse. Transfer image buffers once, run many generations per worker tick, and send compact progress back.

### Phase 3: Minimal C++ Engine Parity

Status: started

Purpose: replace the TypeScript optimizer path with a functioning worker-hosted C++ engine.

Deliverables:

- port image preprocessing into C++
- port target representation into C++
- port dot storage, mutation, rasterization, and fitness evaluation into C++
- expose a plain C ABI for the browser wrapper
- run one image end-to-end through worker + WASM in the browser
- build the same engine natively for a simple CLI proof of concept

Important design rule:

- Do not recreate the current TypeScript object model one class at a time. Use data-oriented arrays for cache locality and predictable memory behavior.

### Phase 4: Incremental Fitness

Status: not started

Purpose: replace the current full-raster full-population recompute with a materially stronger fitness engine.

Deliverables:

- store per-individual coverage or occupancy state
- cache an error score per individual
- track dirty regions caused by mutation
- update only affected pixels when dots move, change radius, or are inserted/removed
- add correctness tests comparing incremental updates against full recomputation

Implementation direction:

- Each individual should own a buffer that can be updated locally when a mutation occurs.
- The engine should support a fallback full recompute path for validation and debugging.

### Phase 5: Multiscale Optimization

Status: not started

Purpose: improve convergence speed and make the system more sophisticated than a brute-force single-scale search.

Deliverables:

- build a multiscale target pyramid in C++
- run evolution at low resolution first
- project elites upward to higher resolutions
- scale and jitter dots between levels
- expose scale-by-scale progress in the worker/UI

Suggested first schedule:

- `1/8 -> 1/4 -> 1/2 -> 1x`

### Phase 6: Search Strategy Upgrades

Status: not started

Purpose: replace simplistic search logic with a more defensible optimization system.

Deliverables:

- importance-weighted initialization from the target image / importance map
- adaptive mutation schedules based on stagnation and recent improvement
- local search or hill-climbing on top elites
- island populations with periodic migration or restart strategies
- configurable exploration vs exploitation controls

Replace:

- the current center-pixel crossover heuristic with a richer selection and local-improvement strategy

### Phase 7: Better Preprocessing and Dot Density Control

Status: not started

Purpose: improve how target structure is converted into stipple priorities.

Deliverables:

- replace naive box blur with a stronger method such as separable Gaussian blur or integral-image blur
- compute gradient magnitude or edge-aware structure measures
- build an importance map from darkness, local structure, and reconstruction error
- derive dot count and initial dot distribution from importance mass rather than black-pixel percentage alone

Important outcome:

- Dot placement should be driven by image structure, not just thresholded darkness.

### Phase 8: Measurability and Reproducibility

Status: not started

Purpose: make the project demonstrably engineering-driven rather than visually interesting only.

Deliverables:

- seeded RNG for reproducible runs
- per-generation timing and stage timing
- memory tracking
- convergence history
- quality metrics such as MSE and SSIM
- saved run configurations and result snapshots
- benchmark mode in both browser and CLI

Outputs:

- machine-readable benchmark JSON
- saved image snapshots for regression comparison
- reproducible seed/config pairs for demos and performance claims

### Phase 9: Outputs That Matter

Status: not started

Purpose: turn the engine into a useful tool, not just an interactive demo.

Deliverables:

- SVG export
- high-resolution PNG export
- timelapse generation
- batch/CLI mode for processing folders
- snapshot save/load support for long runs

Why this matters:

- The CLI plus export pipeline gives the C++ engine value outside the browser, which justifies the architecture.

### Phase 10: Tests, CI, and Final Repo Sharpness

Status: started

Purpose: make the system look finished and trustworthy.

Deliverables:

- C++ unit tests for rasterization, mutation, importance-map generation, and incremental fitness
- regression tests against golden outputs
- TypeScript tests for worker protocol and UI integration boundaries
- GitHub Actions for web build, WASM build, native CLI build, tests, and benchmark smoke runs
- documented developer setup for both browser and native builds

## Commenting and Documentation Standard

This needs to be explicit in the plan: the new code should be understandable to someone who did not write it.

Rules:

- Add comments for non-obvious logic, invariants, and performance-sensitive decisions.
- Do not add noise comments that restate the next line.
- Add short module-level comments at the top of major C++ files to explain purpose and ownership.
- Document worker message types and payload semantics clearly in TypeScript.
- Document memory ownership and lifetime rules at the JS/WASM boundary.
- Add comments around incremental-fitness bookkeeping, dirty-region updates, multiscale transitions, and any heuristic schedules.
- Public C++ APIs and exported WASM functions should have concise doc comments describing inputs, outputs, and constraints.
- Keep comments current when behavior changes. Stale comments are worse than missing comments.

Places where comments matter most:

- rasterization and incremental update logic
- importance-map generation
- mutation and local-search heuristics
- island migration and restart strategy
- snapshot/export serialization
- benchmark and metric collection

## C++ Design Rules

- Prefer structure-of-arrays over object-heavy layouts.
- Use fixed-width numeric types where appropriate.
- Make RNG deterministic and seedable.
- Keep buffers contiguous and reuse memory aggressively.
- Avoid unnecessary copies across the JS/WASM boundary.
- Start with single-threaded WASM first; add SIMD and threads later only after measurement.
- If threading is added later, document required cross-origin isolation constraints for browser deployment.

## Acceptance Criteria

The migration is successful when:

- the UI stays responsive during optimization
- the worker-hosted WASM path clearly outperforms the current TypeScript baseline
- the same seed and config produce the same output in browser and CLI
- multiscale mode converges faster than single-scale mode on benchmark inputs
- incremental fitness matches full recompute correctness checks
- SVG, PNG, and timelapse exports work from saved snapshots
- the repo has tests, CI, fixtures, and cleaned-up documentation
- a new engineer can read the code and understand the critical paths without reverse-engineering everything

## Next Execution Slice

The next concrete slice of work is:

1. route browser image/load/start/stop flow through the worker boundary
2. replace the worker stub with a real adapter that can run the current algorithm off the UI thread
3. keep the adapter interface stable so the TypeScript backend can later be swapped for the C++/WASM backend
4. begin pushing preprocessing and target representation toward the worker/native side

That keeps the codebase moving toward the C++ architecture without blocking on the full native port.
