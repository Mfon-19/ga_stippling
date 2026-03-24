# Optimizer Internals

This document explains how the native stippling optimizer works today and where
to look when changing its behavior.

Relevant files:

- [optimizer.hpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/include/stippling/engine/optimizer.hpp)
- [optimizer.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/optimizer.cpp)
- [engine.hpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/include/stippling/engine/engine.hpp)
- [engine.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/engine.cpp)
- [raster_grid.hpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/include/stippling/engine/raster_grid.hpp)
- [raster_grid.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/raster_grid.cpp)

## Mental Model

The system is split into two layers:

- `Engine` owns preprocessing, the multiscale pyramid, export surfaces, and
  promotion between pyramid levels.
- `Optimizer` owns the population, candidate evaluation, incremental raster
  bookkeeping, and the search heuristics inside one pyramid level.

Each optimizer `Candidate` owns:

- a list of dots
- a `RasterGrid`
- cached `squared_error`
- cached `fitness`

That ownership model is the key performance decision. Mutation and crossover
operate by asking the candidate's raster grid for the error delta of replacing
one dot with another, instead of redrawing the full image after every proposal.

## Determinism Contract

The project relies on deterministic behavior for:

- native CLI vs WASM parity checks
- benchmark comparisons across commits
- seed-based reproducibility in the browser and CLI

Because of that, the optimizer uses:

- an internal seeded RNG
- deterministic multiscale schedules
- deterministic island migration
- deterministic candidate ordering via `candidate_better`

`candidate_better` is intentionally a total ordering, not just a "higher
fitness wins" check. When fitness and squared error are tied, it falls back to
lexicographic dot comparison so different runtimes choose the same champion.

## Preprocessing and Importance

The engine prepares two parallel target representations:

- `optimizer_target_`: blurred grayscale quantized to bytes, then thresholded
  for the target image shown to the user and used as the binary raster
  objective
- `importance_map_`: a richer weighting surface built from darkness, edge
  response, and local structure

The importance map is not the objective itself. It is a search prior used for:

- recommended dot counts
- guided initialization
- guided mutation
- local search scoring

This separation matters. The optimizer is still trying to match a thresholded
black/white target, but it spends more search effort on regions likely to carry
visual detail.

## Multiscale Flow

The pyramid is built in [engine.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/engine.cpp)
with a fixed schedule:

- `1/8`
- `1/4`
- `1/2`
- `1x`

Each level stores:

- resampled target bytes
- resampled importance weights
- its own width and height

When a level starts:

- dot count is scaled by image area
- the optimizer is seeded either from scratch or from projected dots from the
  previous level

When a level is active:

- the engine keeps `projected_best_dots_` updated in full-image coordinates
- UI previews and exports can work without needing to know the current pyramid
  level

When a level is promoted:

- only the best dots are projected upward
- diversity is reintroduced inside `Optimizer::initialize_population()`
- the first candidate preserves the projected champion exactly
- the rest of the population jitters around those seeds

Promotion currently happens when a level has run for a minimum number of
generations and `Optimizer::ready_to_promote_for_multiscale()` decides that the
level has either converged enough or stalled enough.

## One Generation in the Optimizer

Inside [optimizer.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/optimizer.cpp),
`Optimizer::evolve_batch()` repeats this loop:

1. Preserve elites.
2. Refine the top few elites with local search.
3. Fill the rest of the next population with crossover children.
4. Mutate each child using adaptive mutation pressure.
5. Optionally migrate champions across islands.
6. Refresh progress and update stagnation state.
7. Trigger restart logic if the run has stalled.

This is not a pure GA in the textbook sense. It is a hybrid evolutionary search
with local improvement layered in.

## Initialization

Initialization is weighted toward useful regions:

- if the run is seeded from a lower pyramid level, those seeds are reused first
- otherwise most dots come from `guided_dot()`
- a smaller share come from `random_dot()` to maintain exploration

`guided_dot()` samples from a cumulative distribution built by
`build_target_sampler()`. The sampler weights darkness more than importance, but
importance still affects where dots are likely to land and how much jitter they
get around sampled pixels.

## Crossover

Crossover begins from the fitter parent and then tries to import useful dots
from the secondary parent.

Important detail: dot indices do not have stable semantic meaning across
parents. Because of that, crossover does not copy slot `i` from parent A and
slot `i` from parent B. Instead it:

- samples a secondary-parent dot
- optionally blends it with an anchor from the primary parent
- finds a plausible replacement location in the child
- evaluates the replacement through incremental raster error

`find_replacement_index()` prefers:

- overlapping dots first, because they are likely redundant
- otherwise a weak nearby dot, using a simple target-score-plus-distance metric

This keeps crossover spatially meaningful without paying for a full assignment
problem between parent dot sets.

## Mutation and Local Search

Mutation uses three behaviors:

- local search around the current dot
- guided reseeding from the importance map
- fully random reseeding

As stagnation rises:

- mutation rate increases
- movement radius increases
- exploratory acceptance becomes more permissive

`refine_candidate()` is a separate lightweight hill-climbing pass applied to
elites and some newly created children. It preferentially targets weak dots and
tries a small number of local improvements before giving up.

## Islands and Restarts

The optimizer uses island-aware selection for larger populations.

- small populations run as one island
- medium populations split into two islands
- larger populations split into four islands

Most tournaments sample within one island, but a small global sampling chance
lets successful structures spread more broadly. Every few generations,
`migrate_islands()` rotates each island's champion into the next island's
weakest slot.

If the run stalls for long enough, `apply_restart_strategy_if_needed()` replaces
the weakest slice of the population with candidates built from:

- a subset of the current champion
- local perturbations around those champion dots
- fresh guided/random proposals

This is intentionally a partial restart, not a hard reset.

## Incremental Fitness and Reverts

The most fragile logic in the optimizer is the reversible incremental raster
path.

Many operations work like this:

1. Ask `RasterGrid` to apply a hypothetical dot replacement.
2. Read the resulting squared error.
3. Accept or reject the proposal.
4. If rejected, explicitly apply the reverse delta to restore the previous
   raster state.

That revert step is required because `RasterGrid` mutates in place to compute
the hypothetical outcome. Missing a revert causes candidate pixels, coverage
counts, and squared error to drift apart.

When changing any search operator, keep this invariant:

- `candidate.dots`
- `candidate.grid`
- `candidate.squared_error`

must always describe the same image.

## Validation and Regression Hooks

There are two main correctness backstops:

- `Optimizer::validate_incremental_state()` redraws each candidate from scratch
  and compares it against the incremental raster state
- parity and benchmark scripts exercise native CLI and WASM paths on tracked
  fixtures

Use the validation path whenever you change:

- dot replacement logic
- mutation acceptance logic
- raster-grid delta semantics
- multiscale seed projection

## Where To Change What

If you want to change search behavior:

- edit [optimizer.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/optimizer.cpp)

If you want to change preprocessing or importance weighting:

- edit [engine.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/engine.cpp)

If you want to change promotion rules or dot projection between levels:

- edit [engine.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/engine.cpp)

If you want to change the low-level incremental raster contract:

- edit [raster_grid.cpp](/Users/mfonudoh/Desktop/Programs/stippling/cpp/engine/src/raster_grid.cpp)
  and rerun validation/parity checks

## Safe Change Checklist

Before merging optimizer changes:

- build native tests with `ctest --test-dir cpp/build --output-on-failure`
- run `npm run build`
- run `npm run parity:check`
- run `npm run benchmark:fixtures`

If the change touches incremental updates, also confirm that validation output
stays clean through the native CLI `--validate` path.
