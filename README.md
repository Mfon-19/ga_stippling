# Image to stippling art

This is a website that takes in an image and converts it into stippling art using a genetic algorithm. I initially built this project for my Applied AI class a few semesters ago — it was written in TypeScript then, and it was *very* slow. I've been using a lot of C++ lately, and I wanted to make this faster with my newfound C++ powers. The computational core now compiles to WebAssembly for the browser and runs natively through a CLI.

This README is a detailed explanation of how it was built. Hope you enjoy it!

## What is stippling?

Stippling is an artistic technique where images are created entirely from small dots. Instead of using lines or continuous shading, the artist places individual dots — varying their density and spacing to create the illusion of tone, depth, and form. Areas with more tightly packed dots appear darker, while sparser regions read as lighter. It's a technique traditionally done by hand with pen and ink, and it takes a *lot* of patience.

This project automates that process using a genetic algorithm — letting the computer figure out where to place thousands of dots to best reproduce a source image.

![Stippling example](stippling-example.jpg)

## What is a genetic algorithm?

A genetic algorithm (GA) is an optimization technique inspired by natural selection. The idea is simple: start with a population of random "candidates," evaluate how good each one is (its *fitness*), then let the best ones survive and reproduce — mixing traits through *crossover* and introducing small random changes through *mutation*. Over many generations, the population converges toward better and better solutions.

Here's the cycle:

1. **Initialize** — generate a random population of candidate solutions
2. **Evaluate** — score each candidate against the objective (fitness function)
3. **Select** — pick the best-performing candidates as parents
4. **Crossover** — combine traits from two parents to create offspring
5. **Mutate** — introduce small random changes for exploration
6. **Repeat** — the next generation replaces the old, and the cycle continues

GAs are used all over the place in real-world applications:

- **Engineering design** — optimizing aerodynamic shapes, antenna configurations, and structural layouts
- **Logistics & scheduling** — solving vehicle routing, timetabling, and resource allocation problems
- **Finance** — portfolio optimization and trading strategy discovery
- **Bioinformatics** — protein folding, gene sequence alignment, and drug design
- **Game AI** — evolving behaviors and strategies for non-player characters
- **Machine learning** — hyperparameter tuning and neural architecture search

In this project, each "candidate" is a set of dot positions and sizes. The fitness function measures how closely the dots reproduce the target image. Over hundreds of generations, the dots migrate toward an arrangement that looks like the original photo — rendered entirely in stipple art.

So why use a genetic algorithm here instead of, say, gradient descent or a neural network? A genetic algorithm is a good fit here because the optimization landscape is rugged, high-dimensional, and only awkwardly differentiable under the project’s rasterized, thresholded dot representation. Each candidate is easy to score, but small changes in dot positions, sizes, and overlaps can produce discontinuous changes in image error, which makes straightforward gradient-based optimization less natural. A GA avoids that by searching through mutation, recombination, and selection rather than relying on derivatives. That said, gradient-based methods could work with a differentiable rendering formulation, and neural networks would be more useful as learned initializers or proposal models than as direct replacements for the optimizer. I was genuinely fascinated when I first learned about genetic algorithms in class — the idea that you could evolve a solution the same way nature evolves organisms felt almost too elegant. This project was my excuse to actually build something with the technique.

## The flow

```mermaid
sequenceDiagram
    participant User
    participant Browser
    participant Worker
    participant NativeEngine

    User->>Browser: Upload image
    Browser->>Worker: "Preprocess this image"
    Worker->>NativeEngine: Forward RGBA bytes
    NativeEngine->>NativeEngine: Grayscale → Blur → Edges → Importance Map → Threshold
    NativeEngine->>Worker: Processed target + recommended dot count
    Worker->>Browser: Target image + stats
    Browser->>Browser: Display processed target

    User->>Browser: Click "Start Evolution"
    Browser->>Worker: Run config (dots, population, seed)
    Worker->>NativeEngine: Configure + Initialize optimizer
    
    loop Evolution Batches
        Worker->>NativeEngine: Step N generations
        NativeEngine->>NativeEngine: Mutate, crossover, evaluate (incremental raster)
        NativeEngine->>Worker: Best dots + fitness
        Worker->>Browser: Throttled progress + dot snapshot
        Browser->>Browser: Render dots on canvas
    end

    User->>Browser: Click "Export SVG"
    Browser->>Worker: Export request
    Worker->>NativeEngine: Generate SVG
    NativeEngine->>Worker: SVG bytes
    Worker->>Browser: Download file
```

## Steps

### 1. Preprocessing

Before any optimization happens, the input image needs to be transformed into something the optimizer can work with. The whole pipeline runs inside [`prepare_target()`](./cpp/engine/src/engine.cpp#L556-L590) in `engine.cpp`.

**Grayscale** — the engine first turns the image into a simple brightness map using the standard luminance formula (`0.299R + 0.587G + 0.114B`), so every pixel becomes just "how light or dark is this?" See [`extract_grayscale_channel()`](./cpp/engine/src/engine.cpp#L67-L86).

**Blur** — it softly smooths the image with a separable Gaussian pass so tiny specks and noise matter less. This helps the optimizer focus on the big shapes first instead of chasing every pixel. The blur amount is user-configurable via a slider. See [`apply_separable_blur()`](./cpp/engine/src/engine.cpp#L117-L155).

**Edges** — it finds places where brightness changes quickly in the *blurred* image (not the raw original), like outlines, borders, and sharp facial features. Those are spots where dots usually matter more. This uses a Sobel filter computing horizontal and vertical gradients. See [`compute_edge_response()`](./cpp/engine/src/engine.cpp#L157-L186).

**Local Structure** — it compares the original grayscale image to the blurred one to see what fine detail got smoothed away. That highlights texture, hair, fabric, and other small local variation. See [`compute_local_structure()`](./cpp/engine/src/engine.cpp#L188-L198).

**Importance Map** — it combines darkness, edges, and texture into one "where should dots matter most?" map, weighted at 55% darkness, 30% edge energy, and 15% local structure. Dark regions matter, but sharp edges and fine detail get extra weight so the image doesn't turn into just blobs. A small darkness floor ensures broad shadows don't vanish when edge energy is low. Note that the darkness term here is based on the *blurred* grayscale values, not the raw original. See [`combine_importance()`](./cpp/engine/src/engine.cpp#L200-L223).

**Quantize and Threshold** — the blurred image is rounded into byte values ([`quantize_channel()`](./cpp/engine/src/engine.cpp#L225-L231)) so the optimizer has a stable grayscale target to compare against. Then a separate black/white version is created via [`threshold_channel()`](./cpp/engine/src/engine.cpp#L233-L243) for display and statistics, so the UI can show a clean processed target and estimate how many dots are reasonable.

**Target Stats** — the engine counts how much of the image is dark and how much total importance exists, then uses that to suggest a dot count. The recommendation balances importance mass as the primary demand signal, a global area cap so dark images don't explode the workload, and a small black-pixel floor so sparse silhouettes still get enough dots. See [`calculate_target_stats()`](./cpp/engine/src/engine.cpp#L277-L313).

### 2. Multiscale Search

Instead of trying to place thousands of dots correctly on the full image from the start, the engine first learns the big shape cheaply, then keeps refining it as the image gets larger. This is called **multiscale optimization**, and it works through a "pyramid" — which is just the same image prepared at several sizes, from tiny to full size.

The pyramid is built from `optimizer_target_`, which is the quantized blurred grayscale target — not the thresholded black/white preview image. Each level stores a downsampled copy of both the target and the importance map. The whole pyramid is constructed inside [`initialize_optimizer()`](./cpp/engine/src/engine.cpp#L598-L649) using a fixed scale schedule:

| Level | Scale | What it learns |
|-------|-------|----------------|
| 1 | 1/8× | Where the mass goes — broad silhouette and major dark regions |
| 2 | 1/4× | Regional structure and weight distribution |
| 3 | 1/2× | Local corrections, medium detail |
| 4 | 1× | Where the details go — fine edges, small features, final precision |

Each level's target and importance map are downsampled using area averaging ([`resample_u8_average()`](./cpp/engine/src/engine.cpp#L340-L395) and [`resample_double_average()`](./cpp/engine/src/engine.cpp#L403-L449)) rather than point sampling, which preserves coverage mass and prevents thin structures from getting aliased away at coarse scales.

**Dot budget scaling** — coarse levels don't need the full dot count. The engine scales the budget by `sqrt(level_area / full_area)` inside [`initialize_level_optimizer()`](./cpp/engine/src/engine.cpp#L651-L685). At 1/8× width and 1/8× height, the area is 1/64th of full resolution, so the dot count becomes roughly 1/8th of the full budget. Coarse levels should be cheap and structural, not fully detailed.

**Promotion** — after each evolution batch, [`maybe_promote_level()`](./cpp/engine/src/engine.cpp#L687-L723) checks whether the current level is ready to move on. Promotion requires both a minimum generation count and the optimizer itself reporting readiness. When it fires, the best dots are scaled into the next level's coordinate space via [`scale_dots_between_spaces()`](./cpp/engine/src/engine.cpp#L455-L483), a brand new `Optimizer` is created for the finer level seeded with those projected dots, and diversity is rebuilt around them. The engine replaces the optimizer instance entirely on promotion — it's not the same optimizer continuing, it's a fresh one with a strong head start.

**Full-resolution projection** — callers (the browser UI, CLI, export) always receive dots in original image coordinates, regardless of which pyramid level is active. [`best_dots()`](./cpp/engine/src/engine.cpp#L742-L757) handles this transparently: if the optimizer is still on a coarse level, it projects the dots upward via [`project_dots_to_image_space()`](./cpp/engine/src/engine.cpp#L777-L783) so previews just work without knowing about pyramid internals.

### 3. Hybrid Evolutionary Optimization

At each pyramid level, a population of candidate dot arrangements evolves inside [`optimizer.cpp`](./cpp/engine/src/optimizer.cpp). The evolution loop runs in [`evolve_batch()`](./cpp/engine/src/optimizer.cpp#L171-L210), and each generation follows this order: preserve elites → refine elites → breed children → migrate islands → update stagnation → maybe restart.

**Initialization** — when the population is first created in [`initialize_population()`](./cpp/engine/src/optimizer.cpp#L329-L361), dots are not placed randomly. 90% are placed via [`guided_dot()`](./cpp/engine/src/optimizer.cpp#L665-L690), which samples from a precomputed weighted distribution built in [`build_target_sampler()`](./cpp/engine/src/optimizer.cpp#L310-L326). That sampler mixes 65% darkness with 35% importance — so dark regions still dominate placement, but edges and texture pull extra dots toward them. The remaining 10% of dots are fully random for exploration. When seed dots exist from a coarser level, the first candidate preserves them exactly, while the rest of the population fans out around those seeds with jitter via `local_search_dot()`.

**Fitness** — each candidate stores its own dots, its own persistent `RasterGrid`, a `squared_error`, and a `fitness` score. The optimizer works against `target_`, which is the prepared grayscale target bytes — not the thresholded black/white preview. Fitness is computed in [`update_candidate_fitness()`](./cpp/engine/src/optimizer.cpp#L384-L390) as `sqrt(1 - squared_error / max_possible_error)`, where the square root softens the scale so progress is easier to interpret. Deterministic tie-breaking in [`candidate_better()`](./cpp/engine/src/optimizer.cpp#L40-L72) falls through to comparing dot coordinates lexicographically, ensuring native and WASM always select the same champion.

**Elite preservation** — [`evolve_batch()`](./cpp/engine/src/optimizer.cpp#L171-L210) first computes `elite_count` from `config_.elitism_ratio`, then [`preserve_elites()`](./cpp/engine/src/optimizer.cpp#L466-L476) sorts the population and keeps the top `elite_count` candidates. The top 3 elites then get extra local hill-climbing passes via [`refine_elites()`](./cpp/engine/src/optimizer.cpp#L478-L487), which calls [`refine_candidate()`](./cpp/engine/src/optimizer.cpp#L766-L813). Refinement finds weak dots (low target score), proposes replacements, and accepts them if the raster error decreases *or* if the replacement dot's target score is meaningfully better (> 1.05× the current dot's score) — it's not strictly greedy.

**Parent selection — island-aware tournaments** — the population is partitioned into islands (2 for populations ≥ 16, 4 for ≥ 48) via [`island_count_for_population()`](./cpp/engine/src/optimizer.cpp#L83-L90). [`select_parent()`](./cpp/engine/src/optimizer.cpp#L562-L597) runs tournament selection of size 4. Most samples stay within one island, but as stagnation rises, the probability of sampling globally increases (`0.12 + min(0.1, stagnation × 0.01)`) — letting successful structures spread without collapsing diversity too fast.

**Crossover — spatially aware** — [`make_child()`](./cpp/engine/src/optimizer.cpp#L493-L556) starts from the fitter parent as a clone, then attempts to import dots from the secondary parent. For each attempt it: (1) picks a random dot from the secondary parent, (2) optionally blends it with an anchor from the primary parent (40% chance) by averaging positions and radii, or applies a small local perturbation, (3) finds a replacement slot in the child via [`find_replacement_index()`](./cpp/engine/src/optimizer.cpp#L735-L764), which first looks for overlapping dots, then falls back to a nearby weak dot (only sampling 16 candidates, not all dots), (4) evaluates the swap via `apply_dot_delta_and_update_error()` — which mutates the raster in-place to compute the hypothetical next error. Acceptance isn't strictly "did error decrease" — it can also accept if the proposal's target score is meaningfully better, or with a small stagnation-dependent exploratory probability. Rejected proposals must be explicitly reverted to keep raster state in sync.

**Mutation — adaptive** — [`mutate()`](./cpp/engine/src/optimizer.cpp#L819-L864) iterates all dots. Both the mutation rate ([`adaptive_mutation_rate()`](./cpp/engine/src/optimizer.cpp#L647-L651)) and the motion distance ([`mutation_distance_scale()`](./cpp/engine/src/optimizer.cpp#L653-L656)) increase with stagnation. Three mutation modes compete: 55% local search around the current dot, 30% reseed from the guided sampler (darkness + importance weighted), and 15% fully random. Like crossover, acceptance can be exploratory — there's a stagnation-dependent probability of accepting a mutation even if error increased. This lets the same codepath handle both refinement and exploration without hard-coding separate phases.

**Island migration** — [`migrate_islands()`](./cpp/engine/src/optimizer.cpp#L599-L637) runs every 4 generations. Each island's champion replaces the weakest member of the next island in a deterministic ring rotation — cheap, deterministic, and enough to share breakthroughs without homogenizing the whole population.

**Partial restarts** — [`apply_restart_strategy_if_needed()`](./cpp/engine/src/optimizer.cpp#L415-L464) fires after sustained stagnation: 8 generations if the level is narrow (`width_ < 96`), 10 otherwise. It replaces the weakest ~20% of the population with new candidates that use up to 25% of the champion's dots as a starting point — but those dots are *perturbed* through `local_search_dot()`, not copied verbatim. The rest of each replacement candidate is filled from guided and random proposals. The stagnation counter is halved (not reset to zero), so repeated stagnation can trigger further restarts. This lets the search escape a fitness basin without discarding everything it has learned.

### 4. Incremental Raster Fitness

This is the biggest performance win in the entire project, and it all lives in [`raster_grid.cpp`](./cpp/engine/src/raster_grid.cpp).

The original TypeScript version evaluated fitness by re-rendering every candidate from scratch on every generation — painting all dots onto a blank grid, then comparing pixel-by-pixel against the target. That scales linearly with both dot count and image size per evaluation, and it made the old prototype very slow.

The C++ engine takes a completely different approach. Each candidate in the population owns a persistent [`RasterGrid`](./cpp/engine/src/raster_grid.cpp#L18-L26), which stores two parallel arrays: a binary rendered image (`pixels_` — 255 for white, 0 for black) and a per-pixel coverage count (`coverage_` — how many dots currently cover each pixel). Dots are rasterized as filled circles by generating circle extents with a midpoint-circle style integer stepping loop in [`draw_circle()`](./cpp/engine/src/raster_grid.cpp#L103-L134) and then filling horizontal spans through [`update_horizontal_span()`](./cpp/engine/src/raster_grid.cpp#L136-L167). A `delta` parameter controls whether a dot is being added (`+1`) or removed (`-1`).

**Why coverage counts matter** — when two dots overlap and you erase one, the overlapping pixels should stay black because the other dot still covers them. The coverage count tracks exactly how many dots cover each pixel. A pixel is black if `coverage > 0`, white if `coverage == 0`. Without this, erasing one dot would incorrectly whiten pixels that another dot still owns.

**Incremental error updates** — when the optimizer wants to try moving a dot, it calls [`apply_dot_delta_and_update_error()`](./cpp/engine/src/raster_grid.cpp#L46-L58), which performs two sequential incremental passes: first erase the old dot, then draw the new dot. During each pass, `update_horizontal_span()` checks whether each affected pixel actually *changed* value (line 159) — and only then subtracts the old pixel's squared error contribution and adds the new one. The running `squared_error` is updated in-place as the raster changes, so by the time both passes finish, the caller has the exact error the candidate would have if the swap were committed.

**Reversible but not automatic** — the raster is mutated in-place to compute these deltas. If the optimizer decides to reject the proposal, it must explicitly call the function *again* with the dots swapped back. If any codepath forgets to revert, the raster and error silently drift out of sync. That's why the optimizer has a separate [`validate_incremental_state()`](./cpp/engine/src/optimizer.cpp#L228-L289) function and a [`squared_error()`](./cpp/engine/src/raster_grid.cpp#L60-L74) full-redraw path — so tests and CLI commands can prove the incremental bookkeeping hasn't drifted from the reference raster.

This design makes every mutation, crossover, and refinement operation dramatically cheaper. Instead of re-rendering all dots and comparing all pixels, each proposal only touches the pixels under the two dots involved — turning fitness evaluation from O(dots × pixels) to O(dot_footprint).

### 5. Export

Once you're satisfied with the result, the engine can export:

- **SVG** — clean vector output, scalable to any resolution
- **PNG** — rasterized output with a hand-written encoder (dependency-free, deterministic)
- **Timelapse SVG** — an animated SVG showing how the dots evolved over time, assembled from frame snapshots captured during the run


