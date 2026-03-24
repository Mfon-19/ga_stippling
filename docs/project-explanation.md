# Project Explanation

This document is a detailed technical walkthrough of the repository.

It explains:

- what the project is trying to do
- how the architecture is split across browser, worker, WASM, and native code
- how the optimization algorithm works
- why certain design decisions were made
- what the tradeoffs are
- what each important file is responsible for

The goal is to make the codebase legible to someone who did not build it.

## 1. What This Project Is

At the highest level, this is a stippling engine.

It takes an input image, converts it into a simplified black-and-white target,
then searches for a set of circles ("dots") whose rendered output matches that
target as closely as possible.

The original version of the project was a browser-only TypeScript prototype
that ran the genetic algorithm on the main thread. That version was useful for
demonstrating the idea, but it had clear limits:

- the browser UI could stall during evolution
- fitness evaluation was expensive because it redrew each candidate from
  scratch
- the search logic was simple and not very robust
- there was no native CLI, export pipeline, or strong benchmarking story

The current version is the result of a substantial re-architecture.

The heavy compute path now lives in a C++ engine that is:

- compiled to WebAssembly for browser use
- executed inside a Web Worker so the UI remains responsive
- reused natively through a CLI for batch runs, exports, parity checks, and
  benchmarking

This means the project is no longer just "an image art demo." It is now a
small compute platform with:

- a browser frontend
- a worker protocol
- a native optimization core
- a WASM interop layer
- a native CLI
- regression fixtures
- benchmarks
- parity validation
- CI

The original TypeScript GA has been retained under
[archive/typescript-ga](../archive/typescript-ga) for reference and baseline
benchmarking, but it is no longer part of the active runtime path.

## 2. Core Objective

The project solves a constrained image-fitting problem.

The engine does not try to reproduce a full-color, anti-aliased source image.
Instead, it tries to match a thresholded black-and-white target using filled
black circles on a white background.

That is a deliberate simplification.

### Why reduce the target to black and white?

Because the optimizer's primitive is a black dot, not a brush stroke or a tone
field. A binary target aligns with the actual rendering model.

If the target remained full grayscale or full color, the search objective would
be more realistic in one sense, but also more expensive and harder to optimize
with the current dot model.

### Tradeoff

- Benefit: simpler fitness objective, faster raster comparison, easier export
  model, more deterministic behavior
- Cost: less tonal nuance, more abstraction from the original image, and
  visually harsher outputs on some photos

The project partially compensates for that simplification by using a richer
importance map during preprocessing, even though the final optimization target
remains binary.

## 3. End-to-End Runtime Flow

The runtime path is:

1. User uploads an image in the browser.
2. The browser displays the original image on one canvas.
3. The browser asks the worker to preprocess the image.
4. The worker forwards the image into the WASM engine.
5. The native engine:
   - converts the image to grayscale
   - applies blur
   - computes edges and local structure
   - builds an importance map
   - thresholds the grayscale target
   - computes target statistics and a recommended dot count
6. The worker sends the processed image and target stats back to the browser.
7. The browser displays the processed target and updates the dot-count UI.
8. When evolution starts:
   - the browser sends a run config to the worker
   - the worker configures and starts the native optimizer
   - the optimizer runs in batches
   - the worker emits throttled progress and snapshots
   - the browser draws the current best dots to the evolution canvas
9. When the user exports:
   - the worker asks the native engine for SVG or PNG
   - or builds a timelapse SVG from captured frame history
10. For CLI and tests:
   - the same native engine is used directly without the browser

This architecture is the main engineering story of the repo.

The browser is now an orchestration layer. The worker is a scheduling and
messaging layer. The native engine is the real computational core.

## 4. Repository Shape

The most important directories are:

- `src/`: browser code, worker code, protocol, and transitional TS engine
- `cpp/`: native engine, C API, CLI, and tests
- `scripts/`: benchmark and parity tooling
- `fixtures/`: tracked regression inputs
- `docs/`: migration/design documentation
- `benchmarks/`: report conventions and default benchmark config

## 5. Architectural Principles

Several principles show up repeatedly in the codebase.

### 5.1 Keep the UI thin

The browser should be responsible for:

- DOM and canvas wiring
- user interaction
- rendering previews
- initiating runs and exports

The browser should not own heavy optimization work.

### 5.2 Keep the compute boundary explicit

The UI does not call the optimizer directly. It talks to a worker client. The
worker does not poke random native internals. It talks through a typed backend
contract and a plain C ABI.

That explicit boundary is slower to build than a quick hack, but far easier to
stabilize and test.

### 5.3 Reuse one native core everywhere

The same C++ engine powers:

- the browser WASM runtime
- the native CLI
- parity validation
- native tests

That reduces algorithm drift between environments.

### 5.4 Prefer determinism where possible

This project uses seeded random generation and deterministic ordering because:

- benchmarks are more meaningful
- native and WASM outputs can be compared directly
- regressions are easier to reason about

### 5.5 Improve the algorithm and the data path, not just the language

Moving from TypeScript to C++ only helps if the architecture also improves.
This repo intentionally changed:

- execution model: main thread -> worker
- fitness path: full redraw -> incremental raster updates
- optimizer strategy: naive GA -> multiscale hybrid evolutionary search

That is a stronger engineering result than just porting the old TS classes.

## 6. Browser Layer

### [index.html](../index.html)

This file contains the full UI shell and all CSS.

It defines:

- the header and layout
- file upload control
- blur/threshold sliders
- dot count input
- start/stop buttons
- export buttons
- three canvas viewports:
  - original image
  - processed black-and-white image
  - current stippled result

#### Why keep the UI in one HTML file?

Because the project's complexity is primarily computational, not UI-driven.
Using a framework here would add build conventions and state abstractions that
do not clearly improve the core task.

#### Tradeoff

- Benefit: low ceremony, easy to inspect, fast to prototype
- Cost: UI structure and styling are less modular than they would be in a
  componentized frontend

The CSS is intentionally minimal and neutral so the visual layer does not
overshadow the engine work.

### [src/main.ts](../src/main.ts)

This is the browser entrypoint.

Responsibilities:

- instantiate the `CanvasManager`
- collect and validate DOM references
- instantiate `EventHandlers`
- initialize default UI state
- bootstrap the worker client
- handle unload/error cleanup

#### Why bootstrap the worker here?

Because startup is the right place to determine whether the real native/WASM
path is available. If worker or module initialization fails, the app now keeps
processing and optimization unavailable rather than reviving any local fallback.

#### Tradeoff

- Benefit: centralized startup logic and clear runtime gating
- Cost: `main.ts` must know about both UI and worker bootstrap, which couples
  application startup to runtime selection

### [src/ui/CanvasManager.ts](../src/ui/CanvasManager.ts)

This is a thin wrapper around the three canvases and their 2D contexts.

Responsibilities:

- find the three canvas elements
- create 2D contexts
- resize canvases together
- expose the contexts to the rest of the UI layer

#### Why this file exists

Without it, `EventHandlers.ts` would have to manage raw DOM canvas lookups and
context initialization directly, which would make that already-large file even
more cluttered.

#### Tradeoff

- Benefit: small separation of concerns
- Cost: this class is thin enough that it adds a tiny layer of indirection

### [src/ui/EventHandlers.ts](../src/ui/EventHandlers.ts)

This is the main browser orchestration file.

Responsibilities:

- file upload handling
- image loading
- slider changes
- dot count changes
- preprocessing requests
- starting and stopping evolution
- receiving worker progress and snapshots
- drawing dots to the evolution canvas
- updating UI telemetry
- exporting artifacts

This is the browser "application controller."

#### Why it is large

Because it coordinates nearly all browser state:

- loaded image
- processing version
- run ID
- generations
- fitness
- throughput
- export availability

The file is large because it is the meeting point of DOM state, canvas state,
and worker state.

#### Tradeoff

- Benefit: one place to follow end-to-end UI behavior
- Cost: it has become a high-coupling file and is the strongest candidate for
  future refactoring into smaller stateful modules

#### Important design choices

- It uses a `processingVersion` counter to drop stale preprocessing responses.
  This prevents race conditions when a user changes blur/threshold quickly.
- It treats the worker as required for target preparation and evolution.
  That keeps the runtime story simple, but means a failed engine bootstrap
  leaves the processing path unavailable.
- It only enables export buttons when the native backend supports them and a
  run is available.

### [src/utils/config.ts](../src/utils/config.ts)

This file holds shared browser-side constants:

- default population size
- default mutation rate
- elitism ratio
- default blur and threshold
- max dot count
- heuristic constants for recommended dot counts
- canvas element IDs

#### Why centralize these values?

Because they appear across the UI, preprocessing logic, and benchmark tools.
Hard-coding them in multiple places would guarantee drift.

#### Tradeoff

- Benefit: easier tuning and consistency
- Cost: values can still drift from the native engine if both sides are not
  kept in sync carefully

## 7. Shared Browser/Worker Logic

### [src/shared/engineProtocol.ts](../src/shared/engineProtocol.ts)

This file defines the message contract between:

- browser UI
- worker
- runtime backends

It includes:

- engine statuses
- capabilities
- image serialization types
- target-processing config
- run config
- progress events
- snapshot events
- export commands/events

#### Why this file matters

It makes the browser-to-engine boundary explicit and typed.

Without it, the worker layer would be a loose collection of untyped payloads.
That tends to become fragile fast once commands, snapshots, exports, and status
events start to multiply.

#### Tradeoff

- Benefit: maintainable interface boundary
- Cost: more up-front type plumbing

### [src/shared/RasterImageProcessor.ts](../src/shared/RasterImageProcessor.ts)

This is the pure TypeScript preprocessing pipeline used by benchmark tooling.

It performs:

- grayscale extraction
- separable blur
- edge response computation
- local structure computation
- importance-map combination
- thresholding
- recommended dot count estimation

#### Why keep a TS preprocessor if native preprocessing exists?

One reason:

1. Benchmarking the archived TS baseline against the native path only makes
   sense if both start from the same processed target.

#### Tradeoff

- Benefit: consistent benchmark baselines
- Cost: the preprocessing logic is duplicated across TS and C++, which creates
  a maintenance burden

### [src/shared/random.ts](../src/shared/random.ts)

This file defines a tiny random source interface and a deterministic seeded
generator.

#### Why not just use `Math.random()`?

Because benchmarking and parity checks need reproducibility.

The old prototype could tolerate nondeterminism. The current project cannot.

### [src/shared/stippleExport.ts](../src/shared/stippleExport.ts)

This file provides browser-side timelapse SVG assembly.

The native engine exports the current best SVG and PNG directly. Timelapse is
handled in TS because the worker owns the captured frame sequence over time.

#### Why timelapse is outside the native engine

The engine is stateful, but timelapse depends on the worker's scheduling and
frame capture cadence. The worker is the layer that actually knows which
intermediate states were saved.

## 8. The Archived TypeScript Optimizer

The original TypeScript optimizer has been moved to
[archive/typescript-ga](../archive/typescript-ga). It is no longer used by the
active browser runtime, but it remains useful as:

- a historical reference
- a benchmark baseline
- a conceptual reference for how simple the original project was

### [archive/typescript-ga/src/core/Dot.ts](../archive/typescript-ga/src/core/Dot.ts)

Simple dot model with:

- `x`
- `y`
- `radius`

This is intentionally minimal.

### [archive/typescript-ga/src/core/Individual.ts](../archive/typescript-ga/src/core/Individual.ts)

Represents a candidate solution in the old TS GA.

Responsibilities:

- initialize random dots
- mutate dot position and radius
- clone itself

#### Why it is simple

Because the original implementation optimized for understandability, not
performance or sophistication.

#### Tradeoff

- Benefit: easy to follow
- Cost: mutation is naive and not guided by image structure

### [archive/typescript-ga/src/core/Population.ts](../archive/typescript-ga/src/core/Population.ts)

Represents the collection of candidate individuals in the old TS engine.

Responsibilities:

- initialize the population
- prepare the target for fitness evaluation
- compute fitness for each individual
- render individuals to a canvas

#### This file is the original bottleneck

Its fitness strategy is:

1. create a fresh white grid for each candidate
2. draw all dots onto the grid
3. compare the result pixel-by-pixel against the target

That is easy to understand but very expensive.

This file is effectively the "before" picture that justifies the native
incremental raster work.

### [archive/typescript-ga/src/core/GeneticAlgorithm.ts](../archive/typescript-ga/src/core/GeneticAlgorithm.ts)

The old TS evolution loop.

Responsibilities:

- calculate fitness
- preserve elites
- select parents via roulette selection
- crossover per dot index
- mutate

#### Why this logic is weak by modern standards

- crossover assumes dot index `i` in one parent is meaningfully aligned with
  dot index `i` in another parent, which is usually false
- dot "fitness" is approximated by the darkness under the dot center, not by
  actual contribution to image error
- no multiscale strategy
- no local search
- no restart logic
- no incremental fitness

This is not bad for a prototype. It is just not enough for a serious engine.

## 9. Worker Layer

The worker layer exists to separate browser responsiveness from optimizer work.

### [src/worker/WorkerEngineBackend.ts](../src/worker/WorkerEngineBackend.ts)

Defines the worker-facing backend contract shared by:

- the WASM worker backend

This abstraction originally also supported the TypeScript worker backend. The
legacy backend has now been archived, but the interface remains useful because
it keeps the worker/runtime boundary explicit.

### [src/worker/gaWorker.ts](../src/worker/gaWorker.ts)

This is the worker entrypoint.

Responsibilities:

- receive worker commands
- initialize the native WASM module
- route commands to the active backend
- emit progress, snapshot, status, artifact, and error events

#### Important design choice

The worker now treats native WASM initialization as mandatory for evolution.
That simplifies the active runtime path and removes the old archived GA from
browser execution.

### [archive/typescript-ga/src/worker/TypescriptEngineBackend.ts](../archive/typescript-ga/src/worker/TypescriptEngineBackend.ts)

This file is the archived worker wrapper for the old TypeScript optimizer.

Responsibilities:

- preprocess images in the worker
- run generation batches asynchronously
- report fitness and throughput
- emit dot snapshots

#### Why this still exists

It helped the project prove the worker architecture before the native engine was
ready. It is still useful as historical reference and optional benchmark
baseline code, but it is no longer part of the active runtime.

#### Tradeoff

- Benefit: safe migration path and useful benchmark baseline
- Cost: more code to maintain, even though this is not the preferred backend

### [src/worker/WasmEngineBackend.ts](../src/worker/WasmEngineBackend.ts)

Wraps the native WASM engine in worker scheduling logic.

Responsibilities:

- configure the engine
- initialize optimization
- batch evolution steps using `setTimeout`
- compute worker-facing run metrics
- capture frames for timelapse export
- expose SVG/PNG/timelapse artifact export

#### Why batch scheduling is handled here instead of in the native engine

Because the engine should own optimization semantics, not browser event-loop
policy. The worker is the right place to decide how often snapshots are sent
and how frames are captured.

#### Tradeoff

- Benefit: clear separation between engine and runtime orchestration
- Cost: some run lifecycle state now exists outside the engine

## 10. WASM Boundary

### [src/wasm/WasmEngineClient.ts](../src/wasm/WasmEngineClient.ts)

Thin browser-side client over the worker.

Responsibilities:

- generate request IDs
- track pending request promises
- send typed commands to the worker
- route progress and snapshot events to callbacks

#### Why this wrapper exists

Without it, the UI would have to manage low-level worker request/response state
itself, which is error-prone and noisy.

### [src/wasm/engineModule.ts](../src/wasm/engineModule.ts)

This is the most important TypeScript file on the native boundary.

Responsibilities:

- load the generated Emscripten module
- create native engine instances
- allocate and free WASM memory
- copy image bytes into WASM memory
- call exported C functions
- copy processed images, dots, SVG, and PNG bytes out
- turn native failures into JS errors

#### Why a plain C ABI is used instead of richer bindings

Because explicit C-style APIs are:

- easier to keep deterministic
- easier to call from Emscripten
- easier to debug when something goes wrong
- less magical than autogenerated binding systems

#### Tradeoff

- Benefit: very explicit and robust interop
- Cost: verbose manual memory management

### [src/wasm/generated/stipplingEngine.d.ts](../src/wasm/generated/stipplingEngine.d.ts)

Type declaration for the generated Emscripten module.

The generated JS itself is build output, but this type file keeps the
handwritten TS side strongly typed.

## 11. Native Engine: High-Level Design

The native engine is split conceptually into:

- `Engine`: orchestration, preprocessing, multiscale pyramid, exports
- `Optimizer`: search within one pyramid level
- `RasterGrid`: low-level binary raster and incremental error state
- `export`: rendering and metrics
- `c_api`: foreign-function interface for WASM and native tests

This split is intentional.

The optimizer should not have to know how SVG export works. The export layer
should not have to know how island migration works. The C API should not own
business logic.

## 12. Native Engine: Public Interfaces

### [cpp/engine/include/stippling/engine/dot.hpp](../cpp/engine/include/stippling/engine/dot.hpp)

Minimal native dot struct.

It is intentionally simple because the interesting behavior is in how dots are
organized, moved, rasterized, and scored.

### [cpp/engine/include/stippling/engine/engine.hpp](../cpp/engine/include/stippling/engine/engine.hpp)

Defines:

- engine config
- target-processing config
- target stats
- image buffer representation
- capabilities
- engine status
- the `Engine` class

#### Why `Engine` exists separately from `Optimizer`

Because preprocessing, multiscale orchestration, projection, and export are
not optimizer concerns. They are engine concerns.

### [cpp/engine/include/stippling/engine/optimizer.hpp](../cpp/engine/include/stippling/engine/optimizer.hpp)

Declares the native search engine.

Notable internal concepts:

- `Candidate`
- deterministic RNG
- cumulative target sampler
- elite preservation
- refinement
- crossover
- island-aware parent selection
- restarts
- mutation
- validation of incremental raster state

This file now has comments because understanding optimizer behavior from method
names alone is not enough.

### [cpp/engine/include/stippling/engine/raster_grid.hpp](../cpp/engine/include/stippling/engine/raster_grid.hpp)

Declares the low-level binary raster grid.

The important internal data structures are:

- `pixels_`: current rendered binary image
- `coverage_`: overlap counts for each pixel

The overlap counts are critical. Without them, removing one dot could
incorrectly erase pixels that are still covered by another dot.

### [cpp/engine/include/stippling/engine/export.hpp](../cpp/engine/include/stippling/engine/export.hpp)

Declares rendering and quality-metric helpers:

- grayscale rendering
- RGBA rendering
- SVG export
- timelapse SVG export
- PNG export
- image quality metrics

### [cpp/engine/include/stippling/engine/c_api.h](../cpp/engine/include/stippling/engine/c_api.h)

Declares the plain C interface over the engine.

This is the single most important interop contract for:

- WASM calls from JS
- C ABI tests
- explicit error handling

## 13. Native Engine: Preprocessing

### [cpp/engine/src/engine.cpp](../cpp/engine/src/engine.cpp)

This file is large because it owns many responsibilities:

- validate image buffers
- extract grayscale from RGBA or grayscale input
- build a blur kernel
- apply separable blur
- compute edge response
- compute local structure
- combine those signals into an importance map
- quantize and threshold the target
- compute target statistics
- build the multiscale pyramid
- initialize level-specific optimizers
- project dots between level coordinate systems
- promote between pyramid levels
- provide exports and quality metrics

### Why preprocessing is done this way

The preprocessing path builds two different outputs:

1. a blurred, thresholded target image
2. a continuous importance map

The target image is the actual optimization objective.

The importance map is a search prior. It influences where dots should be placed
or moved, but it is not itself the image the optimizer is trying to match.

### Why combine darkness, edges, and local structure?

Because pure darkness is not enough.

If the engine only optimized by darkness mass:

- broad shadows would be overrepresented
- edges and detailed transitions could be undersampled

The current weighting balances:

- darkness
- edge energy
- local structure

This gives the engine a better sense of where dots matter.

### Tradeoff

- Benefit: more intelligent allocation of dots
- Cost: more preprocessing complexity and more parameters that may need tuning

### Recommended dot count logic

The engine computes a recommended dot count based on:

- accumulated importance mass
- a minimum fallback from black-pixel coverage
- an area cap
- a global max-dot cap

This is intentionally more conservative than the old heuristic that could
recommend far too many dots on dark images.

## 14. Native Engine: Multiscale Optimization

Multiscale is implemented in [cpp/engine/src/engine.cpp](../cpp/engine/src/engine.cpp).

The fixed pyramid schedule is:

- `1/8`
- `1/4`
- `1/2`
- `1x`

At each level:

- target bytes are resampled
- importance weights are resampled
- the dot budget is scaled by image area

### Why multiscale exists

Because searching directly at full resolution is inefficient.

At coarse scales the optimizer can learn:

- broad silhouette
- large masses
- major structure

At finer scales it can then spend compute on:

- local corrections
- edge detail
- finer dot arrangement

### Why projected dots are stored

Because the browser and export surfaces always operate in full-image
coordinates. While the engine is still at a coarse level, the UI still needs a
meaningful best-so-far output.

So the engine projects the current best coarse dots up to image space.

### Tradeoff

- Benefit: faster convergence and better search staging
- Cost: added complexity around promotion timing, dot scaling, and projection

## 15. Native Optimizer

### [cpp/engine/src/optimizer.cpp](../cpp/engine/src/optimizer.cpp)

This file contains the actual search behavior.

It is no longer just a simple GA. It is a hybrid search system.

### Candidate representation

Each candidate stores:

- dots
- raster grid
- squared error
- fitness

This is the core performance idea.

The old TypeScript implementation recomputed a whole candidate image from
scratch for every evaluation. The native optimizer instead mutates a candidate's
own raster state incrementally.

### Determinism

The optimizer uses:

- a deterministic PRNG
- deterministic tie-breaking via `candidate_better`
- deterministic island migration

This matters because native and WASM results are compared in parity checks.

### Initialization

Population initialization uses:

- projected seed dots if a coarser level already exists
- mostly guided seeding from the target sampler
- some random dots for exploration

#### Why not pure random initialization?

Because the search would waste too many generations discovering obvious dark or
important regions from scratch.

### Fitness

Fitness is derived from squared pixel error against the binary target.

The code transforms raw squared error into a bounded fitness score using:

- max possible difference
- inversion into a similarity-like score
- square root to soften the scale

This makes progress easier to interpret than raw squared error alone.

### Elite preservation

The optimizer preserves the best fraction of the population each generation.

#### Why preserve elites?

Because random variation can destroy good structures if every generation is
fully replaced.

#### Tradeoff

- Benefit: stable progress
- Cost: too much elitism can reduce diversity

This is why the elitism ratio was reduced from the older, much higher setting.

### Local elite refinement

The top few elites are refined with a local-search pass.

This is one of the places where the optimizer intentionally stops being a pure
GA. The project accepts that impurity because it improves practical search
quality.

### Parent selection

Parent selection uses island-aware tournament selection rather than roulette
selection.

#### Why tournament selection?

Roulette selection is sensitive to small fitness differences and can behave
poorly when the population has low spread. Tournament selection is more stable
and easier to combine with island partitioning.

#### Why islands?

Because independent subpopulations help maintain diversity. If every candidate
competes globally all the time, the search can collapse too quickly.

### Migration

Every few generations, island champions replace the weakest member of the next
island.

This is a deterministic, low-cost way to let good structures propagate without
fully collapsing island diversity.

### Crossover

Crossover begins from the fitter parent and treats the second parent as a
source of useful local proposals.

This is a very important deviation from the old TS algorithm.

The old algorithm assumed that dot slot `i` in one parent corresponded to dot
slot `i` in another parent.

That is usually false.

The native crossover instead:

- samples a secondary-parent dot
- optionally blends it with an anchor from the primary parent
- finds a good replacement candidate in the child
- evaluates the replacement through incremental raster error

#### Why this is better

Because it is spatially meaningful. It treats dots as geometry, not just array
slots.

### `find_replacement_index`

This method looks for:

- overlapping dots first
- otherwise a weak nearby dot

It only samples a subset rather than solving a full assignment problem.

#### Tradeoff

- Benefit: much cheaper than exact matching
- Cost: heuristic, not globally optimal

That is the kind of tradeoff this project makes repeatedly: use simple,
defensible heuristics where exact optimization would be too expensive.

### Mutation

Mutation can:

- do local search around the current dot
- reseed from the guided sampler
- fully randomize

As stagnation increases:

- mutation rate rises
- motion radius rises
- exploratory acceptance becomes more likely

This lets one codepath behave like both a local refiner and an exploration
mechanism.

### Restart logic

If the search stalls long enough, the optimizer replaces the weakest part of
the population with new candidates that are based partly on the champion and
partly on new proposals.

#### Why partial restarts instead of full resets?

Because a full reset throws away too much useful structure.

Partial restart is a compromise:

- keep what the search has learned
- regain diversity
- try to escape the current basin

## 16. Incremental Raster Fitness

### [cpp/engine/src/raster_grid.cpp](../cpp/engine/src/raster_grid.cpp)

This file is the low-level performance foundation of the native optimizer.

Responsibilities:

- maintain binary raster pixels
- maintain overlap counts
- draw and erase dots
- compute squared error
- apply dot replacement deltas incrementally

### Why this file matters so much

Without it, the native engine would still redraw candidates from scratch and
most of the search upgrades would not pay off nearly as much.

### Coverage counts

`coverage_` is the crucial data structure.

If two dots overlap, erasing one dot should not whiten pixels that are still
covered by the other. Coverage counts solve that.

### Reversible updates

Many optimizer operations mutate the raster grid in place to compute a
hypothetical next error.

If the proposal is rejected, the code must explicitly reverse that mutation.

That invariant is fragile but powerful.

It is why:

- incremental fitness is fast
- validation is necessary
- optimizer comments matter

## 17. Export And Metrics

### [cpp/engine/src/export.cpp](../cpp/engine/src/export.cpp)

This file handles:

- grayscale rendering
- RGBA rendering
- SVG export
- timelapse SVG export
- PNG encoding
- quality metrics

### Why exports are in native code

Because:

- the engine already owns the canonical dot state
- native CLI needs the same outputs
- parity checks compare native and WASM exports

Keeping export logic close to the engine makes those comparisons cleaner.

### Handwritten PNG encoder

The PNG path:

- builds RGBA data
- applies simple scanline filtering
- creates a very simple zlib/deflate-compatible stream
- writes PNG chunks manually

#### Why do this instead of using a library?

Because for this project:

- dependency-free export is attractive
- output size is not the main priority
- deterministic behavior is more important than compression quality

#### Tradeoff

- Benefit: small dependency surface and consistent behavior
- Cost: not production-grade image compression

### Quality metrics

The file computes:

- MSE
- RMSE
- PSNR
- exact pixel ratio

These are useful for benchmark reports because fitness alone is engine-specific.
Metrics like MSE and PSNR are easier to compare across implementations.

## 18. C API

### [cpp/engine/src/c_api.cpp](../cpp/engine/src/c_api.cpp)

This file translates between:

- C structs
- C++ engine objects

Responsibilities:

- create and destroy engine handles
- configure engine state
- prepare targets
- initialize and step the optimizer
- expose target stats
- expose prepared image dimensions and bytes
- expose best-dot snapshots
- expose SVG/PNG export buffers
- expose validation state
- expose last error string

### Error strategy

Every ABI function uses an error boundary:

- clear previous error
- execute work
- catch exceptions
- store the message on the engine handle
- return failure code

This is a straightforward and robust strategy for JS/WASM interop.

## 19. CLI

### [cpp/cli/main.cpp](../cpp/cli/main.cpp)

This is a serious CLI, not just a smoke test.

It supports:

- `run`
- `batch`
- `benchmark`

It can:

- load Netpbm images
- preprocess them
- configure and run the engine
- optionally validate incremental state
- emit JSON reports
- export SVG
- export PNG
- export timelapse SVG
- include best-dot snapshots in reports

### Why the CLI matters

It turns the engine into something bigger than a browser toy.

The CLI supports:

- reproducible headless runs
- batch workflows
- easier benchmarking
- export automation
- parity validation against the WASM path

### Netpbm focus

The CLI decodes PGM/PPM/PNM directly.

#### Why not support more formats here?

Because regression and CI inputs should be:

- tiny
- deterministic
- dependency-light

That is exactly what Netpbm gives you.

## 20. Build System

### [cpp/CMakeLists.txt](../cpp/CMakeLists.txt)

This file builds:

- native static engine library
- native CLI
- native tests
- Emscripten WASM target

### Important Emscripten choices

The WASM build uses:

- ES module output
- modularized runtime
- single-file output
- memory growth
- no filesystem
- explicit exported functions
- explicit exported runtime methods

#### Why single-file WASM output?

Because Vite integration is simpler when the worker can import one generated ES
module instead of juggling a separate `.wasm` asset path.

#### Tradeoff

- Benefit: easier browser integration
- Cost: generated JS file is larger and more opaque

## 21. Scripts And Developer Tooling

### [scripts/build-wasm.sh](../scripts/build-wasm.sh)

Builds the WASM engine into its own CMake build tree.

#### Why separate build directories?

Because native and Emscripten builds should not share compiler caches or build
artifacts.

### [scripts/run-benchmark.sh](../scripts/run-benchmark.sh)

Bundles and runs the benchmark script with esbuild.

This keeps the developer workflow simple: benchmark scripts are written in TS,
but run in Node without requiring a heavier runtime compiler setup.

### [scripts/benchmark-headless.ts](../scripts/benchmark-headless.ts)

This is the benchmark harness.

Responsibilities:

- decode fixtures
- preprocess targets consistently
- run both TS and WASM implementations
- estimate a reasonable generation count
- warm up both backends
- repeat runs multiple times
- summarize medians
- compute throughput, time-to-quality, and quality metrics
- emit JSON reports

#### Why use medians and repeated runs?

Because one-off timings are noisy.

### [scripts/parity-check.ts](../scripts/parity-check.ts)

This is the correctness harness between:

- native CLI
- browser/WASM engine

It compares:

- best-dot snapshots
- SVG export
- PNG export
- final fitness
- validation status

#### Why this matters

Because the engine now runs in multiple environments. Without parity checks,
native and WASM could drift silently.

### [scripts/imageDecode.ts](../scripts/imageDecode.ts)

Provides decoding for benchmark/parity scripts.

Behavior:

- decode Netpbm directly in Node
- use Swift for JPEG/AVIF/etc.

### [scripts/decode-image.swift](../scripts/decode-image.swift)

Uses `CoreGraphics` and `ImageIO` to decode richer formats into RGBA.

This is a practical local-dev choice that allows real-photo benchmarking on
macOS without bringing in a Node image dependency.

#### Tradeoff

- Benefit: convenient local benchmarking on JPEG/AVIF
- Cost: not portable to Linux CI, which is why CI focuses on Netpbm fixtures

### [scripts/compare-benchmarks.mjs](../scripts/compare-benchmarks.mjs)

Compares the newest two benchmark reports or two explicitly provided reports.

It focuses on deltas for:

- WASM throughput
- time to target quality
- MSE

This is useful for measuring whether algorithm changes actually helped.

## 22. Tests

### [cpp/tests/smoke_test.cpp](../cpp/tests/smoke_test.cpp)

Verifies that:

- the engine boots
- config is stored correctly
- target preparation works
- target stats are sane
- image loading works

### [cpp/tests/raster_grid_test.cpp](../cpp/tests/raster_grid_test.cpp)

Verifies:

- incremental dot replacement matches full redraw
- overlapping erase behavior is correct
- squared error is nontrivial when dots exist

This is the correctness safety net for the low-level incremental raster path.

### [cpp/tests/c_api_test.cpp](../cpp/tests/c_api_test.cpp)

Verifies:

- the C API can configure the engine
- prepare targets
- initialize the optimizer
- step generations
- expose prepared image bytes
- expose best dots

This ensures the ABI is functionally real, not just theoretically present.

### [cpp/tests/optimizer_test.cpp](../cpp/tests/optimizer_test.cpp)

Verifies deterministic behavior across two identically configured engines.

This is especially important because multiscale promotion, restart behavior,
and tie-breaking could otherwise create nondeterministic divergence.

### [cpp/tests/export_validation_test.cpp](../cpp/tests/export_validation_test.cpp)

Verifies:

- optimizer validation stays clean
- SVG export works
- PNG export works
- C API export paths work

This ties together several surfaces that would otherwise only be checked
indirectly.

## 23. Fixtures

### [fixtures/README.md](../fixtures/README.md)

Explains the purpose of the tracked regression fixtures.

### Regression fixtures

- [fixtures/regression/bands.ppm](../fixtures/regression/bands.ppm)
  Small RGB fixture used to exercise color decoding.
- [fixtures/regression/cross.pgm](../fixtures/regression/cross.pgm)
  Sparse geometric shape useful for parity smoke tests.
- [fixtures/regression/detail-grid.ppm](../fixtures/regression/detail-grid.ppm)
  Higher-detail texture/edge case.
- [fixtures/regression/edge-rings.pgm](../fixtures/regression/edge-rings.pgm)
  Edge-heavy case useful for outline preservation and deterministic ordering.
- [fixtures/regression/portrait-mask.pgm](../fixtures/regression/portrait-mask.pgm)
  Portrait-like silhouette workload.
- [fixtures/regression/sparse-stars.pgm](../fixtures/regression/sparse-stars.pgm)
  Sparse dot-allocation stress case.

These are intentionally tiny so CI can run fast.

### Root benchmark fixtures

- [jobs.jpeg](../jobs.jpeg)
  Real-photo benchmark image, 210x240.
- [landscape.avif](../landscape.avif)
  Real-photo benchmark image, 740x493.

These are more realistic workloads than the tiny tracked fixtures and are used
for stronger local benchmark claims.

## 24. Benchmarking And Reporting

### [benchmarks/README.md](../benchmarks/README.md)

Documents:

- benchmark runs
- fixture smoke benchmarks
- report comparison
- parity checks

### [benchmarks/default-run-config.json](../benchmarks/default-run-config.json)

Stores a canonical run profile for seeded, reproducible benchmarking.

Why store this as JSON?

Because once performance claims matter, benchmark configuration should be
visible and reviewable, not just implied by script code.

## 25. Documentation

### [docs/cpp-wasm-migration-plan.md](../docs/cpp-wasm-migration-plan.md)

Explains how the project moved from:

- browser-only TypeScript prototype

to:

- worker-hosted C++/WASM engine with CLI, exports, parity checks, and CI

It is part technical history and part architecture rationale.

### [docs/optimizer-internals.md](./optimizer-internals.md)

Explains:

- the mental model of the native optimizer
- multiscale flow
- determinism
- crossover
- mutation
- islands
- restarts
- incremental raster reverts

That doc is the best place to start if someone needs to change optimizer
behavior safely.

## 26. Design Tradeoffs

This project made a number of explicit tradeoffs.

### Binary target instead of grayscale/color objective

- Simpler and faster
- Less nuanced visually

### Explicit worker protocol instead of direct calls

- More boilerplate
- Much cleaner architecture

### C API instead of richer bindings

- More manual memory work
- Easier WASM integration and debugging

### Dependency-light native engine

- Simpler portability and determinism
- More handwritten code, including PNG output

### Duplicated preprocessing logic in TS and C++

- Better local preview behavior and benchmark comparability
- Ongoing maintenance burden

### Fixed multiscale schedule

- Deterministic and easy to benchmark
- Less adaptive than a more advanced scheduler

### Hybrid optimizer instead of textbook GA

- Better practical search performance
- Harder to describe as a "pure genetic algorithm"

## 27. What Is Strong About This Codebase

The strongest engineering aspects are:

- clear separation between UI, worker, and native engine
- explicit interop boundary
- deterministic runtime behavior
- reuse of one native core across browser and CLI
- incremental raster fitness instead of naive full redraw
- multiscale optimization
- parity checking between native and WASM
- benchmark tooling that measures real quality/speed deltas
- CI coverage over both build and regression paths

These are the parts that make the project feel more like a serious engine than
just a demo.

## 28. What Is Still Weak Or Costly

The biggest remaining weaknesses are:

- the browser UI orchestration in `EventHandlers.ts` is still too centralized
- some documentation still reflects the project's earlier prototype identity
- TS and C++ preprocessing must be kept in sync manually
- the PNG export path is intentionally simple
- benchmark quality thresholds in CI are still lighter than they could be
- optimizer heuristics are stronger than before, but still heuristic and still
  tunable

Those are not fatal flaws, but they are the most obvious areas for future
improvement.

## 29. If You Are New To The Repo

Read files in this order:

1. [README.md](../README.md)
2. [docs/cpp-wasm-migration-plan.md](./cpp-wasm-migration-plan.md)
3. [docs/optimizer-internals.md](./optimizer-internals.md)
4. [src/main.ts](../src/main.ts)
5. [src/ui/EventHandlers.ts](../src/ui/EventHandlers.ts)
6. [src/shared/engineProtocol.ts](../src/shared/engineProtocol.ts)
7. [src/worker/gaWorker.ts](../src/worker/gaWorker.ts)
8. [src/wasm/engineModule.ts](../src/wasm/engineModule.ts)
9. [cpp/engine/src/engine.cpp](../cpp/engine/src/engine.cpp)
10. [cpp/engine/src/optimizer.cpp](../cpp/engine/src/optimizer.cpp)
11. [cpp/engine/src/raster_grid.cpp](../cpp/engine/src/raster_grid.cpp)
12. [cpp/cli/main.cpp](../cpp/cli/main.cpp)
13. [scripts/benchmark-headless.ts](../scripts/benchmark-headless.ts)
14. [scripts/parity-check.ts](../scripts/parity-check.ts)

That order follows the actual runtime path from browser to worker to native
engine to validation.

## 30. Summary

This repository is best understood as a layered system:

- the browser provides interaction and visualization
- the worker provides responsiveness and runtime isolation
- the native engine provides preprocessing, multiscale search, incremental
  fitness, and export logic
- the CLI, tests, and scripts provide reproducibility and confidence

The most important technical move in the project was not "rewrite it in C++."
It was:

- move compute off the main thread
- define a stable worker/runtime boundary
- build one reusable native core
- replace naive full recomputation with incremental raster updates
- add deterministic measurement and parity validation around the engine

That is why the codebase now reads like an engineering project instead of just
an art experiment.
