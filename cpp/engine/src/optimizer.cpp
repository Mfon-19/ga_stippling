#include "stippling/engine/optimizer.hpp"

// optimizer.cpp implements the native search loop that evolves stipple-dot
// candidates toward a target image.
//
// At a high level, this file is responsible for:
// - owning the optimizer's deterministic random generator and candidate state
// - initializing a population from guided, random, or promoted seed dots
// - scoring candidates against the target with incremental raster/error updates
// - evolving the population through elitism, crossover, mutation, and local
//   refinement
// - tracking stagnation and reacting with adaptive mutation, island migration,
//   and restart logic
// - exposing best-solution snapshots, validation, and progress metrics back to
//   the higher-level Engine orchestration layer
//
// The Engine decides when to build pyramids and promote between resolutions.
// This file focuses on the search performed at one active pyramid level.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace stippling {

namespace {

/** Returns true when two dots have identical geometry. */
bool dots_equal(const Dot& left, const Dot& right) {
  return left.x == right.x && left.y == right.y && left.radius == right.radius;
}

/**
 * Provides a deterministic total ordering for candidates. Fitness is primary,
 * but exact ties are broken by error and dot geometry so native and WASM runs
 * choose the same champion in parity tests.
 */
template <typename CandidateLike>
bool candidate_better(const CandidateLike& left, const CandidateLike& right) {
  constexpr double kFitnessEpsilon = 1e-12;

  // Fitness is the primary ordering, but parity tests need a total ordering for
  // equal-fitness candidates so native and WASM runs pick the same champion.
  if (std::abs(left.fitness - right.fitness) > kFitnessEpsilon) {
    return left.fitness > right.fitness;
  }
  if (left.squared_error != right.squared_error) {
    return left.squared_error < right.squared_error;
  }

  const auto dot_count = std::min(left.dots.size(), right.dots.size());
  for (std::size_t index = 0; index < dot_count; ++index) {
    if (left.dots[index].x != right.dots[index].x) {
      return left.dots[index].x < right.dots[index].x;
    }
    if (left.dots[index].y != right.dots[index].y) {
      return left.dots[index].y < right.dots[index].y;
    }
    if (left.dots[index].radius != right.dots[index].radius) {
      return left.dots[index].radius < right.dots[index].radius;
    }
  }

  return left.dots.size() < right.dots.size();
}

/** Clamps an x/y coordinate into the active raster bounds. */
double clamp_position(double value, int limit) {
  return std::clamp(value, 0.0, static_cast<double>(std::max(0, limit - 1)));
}

/** Clamps a dot radius into the supported stipple footprint range. */
double clamp_radius(double value) {
  return std::clamp(value, 0.35, 1.35);
}

/** Chooses how many migration islands to use for the current population size. */
std::size_t island_count_for_population(std::size_t population_size) {
  if (population_size >= 48) {
    return 4;
  }
  if (population_size >= 16) {
    return 2;
  }
  return 1;
}

}  // namespace

/** Seeds the optimizer's deterministic RNG. */
Optimizer::RandomGenerator::RandomGenerator(std::uint32_t seed)
    : state_(seed) {}

/** Returns the next pseudo-random value in [0, 1). */
double Optimizer::RandomGenerator::next_unit() {
  state_ += 0x6d2b79f5u;
  auto t = state_;
  t = std::uint32_t((t ^ (t >> 15)) * (t | 1u));
  t ^= t + std::uint32_t((t ^ (t >> 7)) * (t | 61u));
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

/** Returns the next pseudo-random 32-bit integer. */
std::uint32_t Optimizer::RandomGenerator::next_u32() {
  return static_cast<std::uint32_t>(next_unit() * 4294967295.0);
}

/** Constructs an optimizer with no promoted seed dots. */
Optimizer::Optimizer(int width,
                     int height,
                     std::vector<std::uint8_t> target,
                     std::vector<double> importance,
                     const EngineConfig& config)
    : Optimizer(width, height, std::move(target), std::move(importance), config, {}) {}

/**
 * Constructs an optimizer for one pyramid level. The target raster and
 * importance map define the search space; optional seed dots come from a
 * coarser multiscale level.
 */
Optimizer::Optimizer(int width,
                     int height,
                     std::vector<std::uint8_t> target,
                     std::vector<double> importance,
                     const EngineConfig& config,
                     std::vector<Dot> seed_dots)
    : width_(width),
      height_(height),
      config_(config),
      target_(std::move(target)),
      importance_(std::move(importance)),
      seed_dots_(std::move(seed_dots)),
      random_(config.seed) {
  if (width_ <= 0 || height_ <= 0) {
    throw std::invalid_argument("Optimizer dimensions must be positive");
  }
  if (target_.size() != static_cast<std::size_t>(width_ * height_)) {
    throw std::invalid_argument("Optimizer target size does not match dimensions");
  }
  if (!importance_.empty() &&
      importance_.size() != static_cast<std::size_t>(width_ * height_)) {
    throw std::invalid_argument(
        "Optimizer importance map size does not match dimensions");
  }
  if (importance_.empty()) {
    importance_.assign(target_.size(), 0.0);
  }
  if (config_.population_size == 0) {
    throw std::invalid_argument("Population size must be positive");
  }
  if (config_.dot_count == 0) {
    throw std::invalid_argument("Dot count must be positive");
  }

  build_target_sampler();
}

/** Initializes the first population and computes its baseline progress state. */
void Optimizer::initialize() {
  initialize_population();
  evaluate_population();
  progress_.generation = 0;
  last_best_fitness_ = progress_.best_fitness;
  stagnation_generations_ = 0;
}

/**
 * Runs one configured batch of generations. Each generation keeps elites,
 * refines a few strong candidates locally, breeds the remainder, then updates
 * migration/stagnation/restart state from the new frontier.
 */
OptimizerProgress Optimizer::evolve_batch() {
  ensure_initialized();

  for (std::uint32_t batch_index = 0; batch_index < config_.generations_per_batch;
       ++batch_index) {
    const auto elite_count = std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::floor(
                static_cast<double>(config_.population_size) *
                config_.elitism_ratio)));
    auto next_population = preserve_elites(elite_count);
    refine_elites(&next_population);

    const auto island_count = island_count_for_population(population_.size());
    while (next_population.size() < config_.population_size) {
      const auto island_index = island_count == 1
                                    ? 0u
                                    : static_cast<std::size_t>(
                                          next_population.size() % island_count);
      const auto& parent_a = select_parent(island_index);
      const auto& parent_b = select_parent(island_index);
      auto child = make_child(parent_a, parent_b);
      if (random_.next_unit() < 0.25) {
        refine_candidate(&child, 2);
      }
      next_population.push_back(std::move(child));
    }

    population_ = std::move(next_population);
    migrate_islands();
    refresh_progress();
    update_search_state();
    apply_restart_strategy_if_needed();
    refresh_progress();
    ++progress_.generation;
  }

  return progress_;
}

/** Reports whether a population has already been initialized. */
bool Optimizer::initialized() const noexcept {
  return !population_.empty();
}

/** Returns the current best candidate's dots. */
const std::vector<Dot>& Optimizer::best_dots() const {
  ensure_initialized();
  const auto best = std::max_element(
      population_.begin(), population_.end(),
      [](const Candidate& left, const Candidate& right) {
        return candidate_better(right, left);
      });
  return best->dots;
}

/** Returns the latest cached progress metrics. */
OptimizerProgress Optimizer::progress() const noexcept {
  return progress_;
}

/**
 * Recomputes each candidate from scratch to prove the incremental raster and
 * squared-error bookkeeping still match the reference implementation.
 */
OptimizerValidation Optimizer::validate_incremental_state() const {
  ensure_initialized();

  OptimizerValidation validation{};
  validation.checked_candidates =
      static_cast<std::uint32_t>(population_.size());
  validation.first_mismatch_index = validation.checked_candidates;

  // This validation path intentionally redraws from scratch so tests and CLI
  // commands can prove that the incremental bookkeeping has not drifted away
  // from the reference raster.
  for (std::size_t candidate_index = 0; candidate_index < population_.size();
       ++candidate_index) {
    const auto& candidate = population_[candidate_index];
    RasterGrid full(width_, height_);

    for (const auto& dot : candidate.dots) {
      full.draw_dot(dot);
    }

    const auto recomputed_error = full.squared_error(target_);
    const auto pixel_match = full.pixels() == candidate.grid.pixels();
    const auto error_match = recomputed_error == candidate.squared_error;

    if (pixel_match && error_match) {
      continue;
    }

    validation.valid = false;
    ++validation.mismatched_candidates;
    if (validation.first_mismatch_index == validation.checked_candidates) {
      validation.first_mismatch_index =
          static_cast<std::uint32_t>(candidate_index);
    }

    const auto error_delta =
        recomputed_error > candidate.squared_error
            ? recomputed_error - candidate.squared_error
            : candidate.squared_error - recomputed_error;
    validation.max_squared_error_delta =
        std::max(validation.max_squared_error_delta, error_delta);

    const auto& expected_pixels = full.pixels();
    const auto& actual_pixels = candidate.grid.pixels();
    for (std::size_t pixel_index = 0; pixel_index < expected_pixels.size();
         ++pixel_index) {
      if (expected_pixels[pixel_index] != actual_pixels[pixel_index]) {
        ++validation.total_pixel_mismatches;
      }
    }
  }

  if (validation.valid) {
    validation.first_mismatch_index = 0;
  }

  return validation;
}

/**
 * Reports whether the current level looks stable enough to promote to a finer
 * multiscale resolution.
 */
bool Optimizer::ready_to_promote_for_multiscale() const noexcept {
  if (!initialized()) {
    return false;
  }

  return progress_.generation >= 2 &&
         (stagnation_generations_ >= 1 || progress_.best_fitness >= 0.92 ||
          progress_.generation >= 6);
}

/** Returns how many generations have passed without a meaningful improvement. */
std::uint32_t Optimizer::stagnation_generations() const noexcept {
  return stagnation_generations_;
}

/** Throws if callers try to evolve or inspect a population before initialization. */
void Optimizer::ensure_initialized() const {
  if (!initialized()) {
    throw std::logic_error("Optimizer population has not been initialized");
  }
}

/**
 * Builds the weighted sampler used for guided seeding and guided mutation.
 * Darkness dominates, while the importance map adds extra pull toward edges
 * and local structure.
 */
void Optimizer::build_target_sampler() {
  cumulative_target_weights_.clear();
  cumulative_target_weights_.reserve(target_.size());
  total_target_weight_ = 0.0;

  for (std::size_t index = 0; index < target_.size(); ++index) {
    const auto darkness = (255.0 - static_cast<double>(target_[index])) / 255.0;
    const auto importance = index < importance_.size() ? importance_[index] : 0.0;
    const auto weight = std::max(0.0, darkness * 0.65 + importance * 0.35);
    total_target_weight_ += weight;
    cumulative_target_weights_.push_back(total_target_weight_);
  }
}

/**
 * Creates the initial population. When seed dots are present, the first
 * candidate preserves them exactly and the rest fan out nearby to restore
 * diversity after multiscale promotion.
 */
void Optimizer::initialize_population() {
  population_.clear();
  population_.reserve(config_.population_size);

  for (std::uint32_t candidate_index = 0;
       candidate_index < config_.population_size; ++candidate_index) {
    Candidate candidate(width_, height_);
    candidate.dots.reserve(config_.dot_count);

    for (std::uint32_t dot_index = 0; dot_index < config_.dot_count; ++dot_index) {
      if (dot_index < seed_dots_.size()) {
        const auto& seed_dot = seed_dots_[dot_index];
        candidate.dots.push_back(candidate_index == 0
                                     ? Dot{
                                           .x = clamp_position(seed_dot.x, width_),
                                           .y = clamp_position(seed_dot.y, height_),
                                           .radius = clamp_radius(seed_dot.radius),
                                       }
                                     : local_search_dot(seed_dot, 1.35, 0.10));
        continue;
      }

      const auto use_guided_seed =
          total_target_weight_ > 0.0 && random_.next_unit() < 0.9;
      candidate.dots.push_back(use_guided_seed ? guided_dot() : random_dot());
    }

    population_.push_back(std::move(candidate));
  }
}

/** Fully evaluates every candidate in the current population. */
void Optimizer::evaluate_population() {
  for (auto& candidate : population_) {
    evaluate_candidate(candidate);
  }

  refresh_progress();
}

/** Rasterizes one candidate from scratch and computes its squared error. */
void Optimizer::evaluate_candidate(Candidate& candidate) const {
  candidate.grid.clear();
  for (const auto& dot : candidate.dots) {
    candidate.grid.draw_dot(dot);
  }

  candidate.squared_error = candidate.grid.squared_error(target_);
  update_candidate_fitness(candidate);
}

/** Converts squared error into the normalized fitness score used for ranking. */
void Optimizer::update_candidate_fitness(Candidate& candidate) const {
  const auto max_diff = static_cast<double>(width_) * static_cast<double>(height_) *
                        255.0 * 255.0;
  const auto raw_fitness =
      1.0 - static_cast<double>(candidate.squared_error) / max_diff;
  candidate.fitness = std::sqrt(std::max(0.0, raw_fitness));
}

/** Refreshes cached best-fitness progress metrics from the current population. */
void Optimizer::refresh_progress() {
  const auto best = std::max_element(
      population_.begin(), population_.end(),
      [](const Candidate& left, const Candidate& right) {
        return candidate_better(right, left);
      });
  progress_.best_fitness = best->fitness;
  progress_.best_squared_error = best->squared_error;
}

/** Updates stagnation tracking after a generation completes. */
void Optimizer::update_search_state() {
  constexpr double kImprovementEpsilon = 1e-6;

  if (progress_.best_fitness > last_best_fitness_ + kImprovementEpsilon) {
    last_best_fitness_ = progress_.best_fitness;
    stagnation_generations_ = 0;
    return;
  }

  ++stagnation_generations_;
}

/**
 * Replaces part of the weakest tail with champion-informed reseeds once the
 * run has stalled for long enough.
 */
void Optimizer::apply_restart_strategy_if_needed() {
  const auto restart_threshold = std::max<std::uint32_t>(8u, width_ < 96 ? 6u : 10u);
  if (stagnation_generations_ < restart_threshold || population_.size() < 4) {
    return;
  }

  std::vector<std::size_t> sorted_indices(population_.size(), 0u);
  std::iota(sorted_indices.begin(), sorted_indices.end(), 0u);
  std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](std::size_t left, std::size_t right) {
              return candidate_better(population_[left], population_[right]);
            });

  const auto champion = population_[sorted_indices.front()];
  const auto restart_count =
      std::max<std::size_t>(1u, population_.size() / 5u);

  for (std::size_t offset = 0; offset < restart_count; ++offset) {
    Candidate replacement(width_, height_);
    replacement.dots.reserve(config_.dot_count);

    const auto champion_seed_count = std::min<std::size_t>(
        champion.dots.size(), std::max<std::size_t>(1u, config_.dot_count / 4u));
    for (std::size_t seed_index = 0; seed_index < champion_seed_count; ++seed_index) {
      replacement.dots.push_back(
          local_search_dot(champion.dots[seed_index], 2.5, 0.18));
    }
    while (replacement.dots.size() < config_.dot_count) {
      replacement.dots.push_back(random_.next_unit() < 0.8 ? guided_dot()
                                                           : random_dot());
    }

    evaluate_candidate(replacement);
    population_[sorted_indices[sorted_indices.size() - 1u - offset]] =
        std::move(replacement);
  }

  stagnation_generations_ /= 2u;
  refresh_progress();
  last_best_fitness_ = progress_.best_fitness;
}

/** Returns the best-scoring prefix of the current population. */
std::vector<Optimizer::Candidate> Optimizer::preserve_elites(
    std::uint32_t elite_count) const {
  auto sorted = population_;
  std::sort(sorted.begin(), sorted.end(),
            [](const Candidate& left, const Candidate& right) {
              return candidate_better(left, right);
            });
  const auto keep_count = std::min<std::size_t>(elite_count, sorted.size());
  sorted.erase(sorted.begin() + keep_count, sorted.end());
  return sorted;
}

/** Applies extra local-search passes to a few top elites before breeding. */
void Optimizer::refine_elites(std::vector<Candidate>* elites) {
  if (elites == nullptr || elites->empty()) {
    return;
  }

  const auto refinement_count = std::min<std::size_t>(3u, elites->size());
  for (std::size_t index = 0; index < refinement_count; ++index) {
    refine_candidate(&(*elites)[index], 5u + index * 2u);
  }
}

/**
 * Builds one child by treating the fitter parent as the base candidate and
 * importing promising local proposals from the secondary parent.
 */
Optimizer::Candidate Optimizer::make_child(const Candidate& parent_a,
                                           const Candidate& parent_b) {
  const auto& primary_parent =
      parent_a.fitness >= parent_b.fitness ? parent_a : parent_b;
  const auto& secondary_parent =
      parent_a.fitness >= parent_b.fitness ? parent_b : parent_a;
  Candidate child = primary_parent;
  const auto import_attempts = std::min<std::size_t>(
      std::max<std::size_t>(6u, config_.dot_count / 12u), 24u);

  for (std::size_t attempt = 0; attempt < import_attempts; ++attempt) {
    const auto secondary_index = static_cast<std::size_t>(
        random_.next_u32() % secondary_parent.dots.size());
    Dot proposal = secondary_parent.dots[secondary_index];

    if (random_.next_unit() < 0.4) {
      const auto anchor_index =
          static_cast<std::size_t>(random_.next_u32() % primary_parent.dots.size());
      const auto& anchor_dot = primary_parent.dots[anchor_index];
      proposal.x = clamp_position((proposal.x + anchor_dot.x) * 0.5, width_);
      proposal.y = clamp_position((proposal.y + anchor_dot.y) * 0.5, height_);
      proposal.radius = clamp_radius((proposal.radius + anchor_dot.radius) * 0.5);
    } else if (random_.next_unit() < 0.65) {
      proposal = local_search_dot(proposal, mutation_distance_scale() * 0.7, 0.08);
    }

    const auto replacement_index = find_replacement_index(child, proposal);
    const auto current_dot = child.dots[replacement_index];
    const auto current_score = dot_target_score(current_dot);
    const auto proposal_score = dot_target_score(proposal);
    if (proposal_score + 4.0 < current_score && random_.next_unit() < 0.9) {
      continue;
    }

    const auto next_error = child.grid.apply_dot_delta_and_update_error(
        current_dot, proposal, target_, child.squared_error);
    const auto accept =
        next_error <= child.squared_error ||
        proposal_score > current_score * 1.08 ||
        random_.next_unit() <
            0.06 + std::min(0.1, stagnation_generations_ * 0.01);
    if (accept) {
      child.squared_error = next_error;
      child.dots[replacement_index] = proposal;
    } else {
      (void)child.grid.apply_dot_delta_and_update_error(
          proposal, current_dot, target_, next_error);
    }
  }

  mutate(child);
  update_candidate_fitness(child);
  return child;
}

/**
 * Chooses a parent with tournament selection. Most tournaments stay within one
 * island, but the sampling widens as stagnation rises so breakthroughs can
 * spread across the full population.
 */
const Optimizer::Candidate& Optimizer::select_parent(std::size_t island_index) {
  constexpr std::size_t kTournamentSize = 4;
  const auto island_count = island_count_for_population(population_.size());
  const auto island = island_count == 0 ? 0u : island_index % island_count;
  const auto island_start = island * population_.size() / island_count;
  const auto island_end = (island + 1u) * population_.size() / island_count;
  const auto sample_global = island_count == 1 ||
                             random_.next_unit() <
                                 0.12 + std::min(0.1, stagnation_generations_ * 0.01);

  auto sample_index = [&]() -> std::size_t {
    if (sample_global) {
      return static_cast<std::size_t>(random_.next_u32() % population_.size());
    }

    const auto span = std::max<std::size_t>(1u, island_end - island_start);
    return island_start + static_cast<std::size_t>(random_.next_u32() % span);
  };

  auto best_index = sample_index();
  for (std::size_t round = 1; round < kTournamentSize; ++round) {
    const auto challenger_index = sample_index();
    if (candidate_better(population_[challenger_index], population_[best_index])) {
      best_index = challenger_index;
    }
  }

  return population_[best_index];
}

/** Periodically rotates one champion from each island into the next island. */
void Optimizer::migrate_islands() {
  const auto island_count = island_count_for_population(population_.size());
  if (island_count == 1 || progress_.generation == 0 || progress_.generation % 4 != 0) {
    return;
  }

  std::vector<Candidate> champions;
  champions.reserve(island_count);
  std::vector<std::size_t> weakest_indices;
  weakest_indices.reserve(island_count);

  for (std::size_t island = 0; island < island_count; ++island) {
    const auto island_start = island * population_.size() / island_count;
    const auto island_end = (island + 1u) * population_.size() / island_count;
    auto best_index = island_start;
    auto worst_index = island_start;

    for (std::size_t index = island_start; index < island_end; ++index) {
      if (candidate_better(population_[index], population_[best_index])) {
        best_index = index;
      }
      if (candidate_better(population_[worst_index], population_[index])) {
        worst_index = index;
      }
    }

    champions.push_back(population_[best_index]);
    weakest_indices.push_back(worst_index);
  }

  for (std::size_t island = 0; island < island_count; ++island) {
    const auto destination_island = (island + 1u) % island_count;
    population_[weakest_indices[destination_island]] = champions[island];
  }
}

/** Samples one target pixel index according to the precomputed target weights. */
std::size_t Optimizer::sample_target_index() {
  if (total_target_weight_ <= 0.0 || cumulative_target_weights_.empty()) {
    return static_cast<std::size_t>(random_.next_u32() % target_.size());
  }

  const auto threshold = random_.next_unit() * total_target_weight_;
  const auto match = std::upper_bound(cumulative_target_weights_.begin(),
                                      cumulative_target_weights_.end(), threshold);
  if (match == cumulative_target_weights_.end()) {
    return cumulative_target_weights_.size() - 1u;
  }

  return static_cast<std::size_t>(
      std::distance(cumulative_target_weights_.begin(), match));
}

/** Increases mutation pressure as stagnation rises, up to a fixed cap. */
double Optimizer::adaptive_mutation_rate() const {
  const auto multiplier =
      1.0 + std::min(2.0, static_cast<double>(stagnation_generations_) * 0.08);
  return std::min(0.75, config_.mutation_rate * multiplier);
}

/** Widens mutation and local-search step sizes as stagnation rises. */
double Optimizer::mutation_distance_scale() const {
  return 1.2 + std::min(6.0, static_cast<double>(stagnation_generations_) * 0.45);
}

/**
 * Creates a dot near a weighted target location, with jitter so multiple dots
 * can spread through an important region instead of collapsing onto one pixel.
 */
Dot Optimizer::guided_dot() {
  const auto target_index = sample_target_index();
  const auto base_x =
      static_cast<double>(static_cast<int>(target_index % static_cast<std::size_t>(width_)));
  const auto base_y =
      static_cast<double>(static_cast<int>(target_index / static_cast<std::size_t>(width_)));
  const auto darkness =
      (255.0 - static_cast<double>(target_[target_index])) / 255.0;
  const auto importance =
      target_index < importance_.size() ? importance_[target_index] : darkness;
  const auto jitter_scale = 0.85 + importance * 2.5;

  return {
      .x = clamp_position(
          base_x + (random_.next_unit() * 2.0 - 1.0) * jitter_scale, width_),
      .y = clamp_position(
          base_y + (random_.next_unit() * 2.0 - 1.0) * jitter_scale, height_),
      .radius =
          clamp_radius(0.4 + darkness * 0.35 + importance * 0.25 +
                       random_.next_unit() * 0.18),
  };
}

/** Scores how promising one dot location looks against the target and importance map. */
double Optimizer::dot_target_score(const Dot& dot) const {
  const auto x = static_cast<int>(std::floor(dot.x));
  const auto y = static_cast<int>(std::floor(dot.y));

  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    return 0.0;
  }

  const auto index = static_cast<std::size_t>(y * width_ + x);
  const auto darkness = 255.0 - static_cast<double>(target_[index]);
  const auto importance =
      index < importance_.size() ? importance_[index] * 255.0 : 0.0;
  return darkness * 0.65 + importance * 0.35;
}

/** Creates an unconstrained random dot anywhere in the current level. */
Dot Optimizer::random_dot() {
  return {
      .x = std::floor(random_.next_unit() * static_cast<double>(width_)),
      .y = std::floor(random_.next_unit() * static_cast<double>(height_)),
      .radius = 0.4 + random_.next_unit() * 0.55,
  };
}

/**
 * Searches a small neighborhood around a dot and returns the locally best
 * proposal according to the dot-level target score.
 */
Dot Optimizer::local_search_dot(const Dot& dot,
                                double distance_scale,
                                double radius_scale) {
  auto best_dot = Dot{
      .x = clamp_position(dot.x, width_),
      .y = clamp_position(dot.y, height_),
      .radius = clamp_radius(dot.radius),
  };
  auto best_score = dot_target_score(best_dot);

  const auto sample_count = 6u;
  for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
    const auto offset_x = (random_.next_unit() * 2.0 - 1.0) * distance_scale;
    const auto offset_y = (random_.next_unit() * 2.0 - 1.0) * distance_scale;
    const auto proposal = Dot{
        .x = clamp_position(best_dot.x + offset_x, width_),
        .y = clamp_position(best_dot.y + offset_y, height_),
        .radius = clamp_radius(
            best_dot.radius + (random_.next_unit() * 2.0 - 1.0) * radius_scale),
    };
    const auto proposal_score = dot_target_score(proposal);
    if (proposal_score > best_score) {
      best_dot = proposal;
      best_score = proposal_score;
    }
  }

  return best_dot;
}

/**
 * Chooses which child dot slot to replace during crossover. It prefers an
 * overlapping neighbor when possible, otherwise a weak nearby dot.
 */
std::size_t Optimizer::find_replacement_index(const Candidate& child,
                                              const Dot& proposal) const {
  if (child.dots.empty()) {
    return 0u;
  }

  auto best_index = static_cast<std::size_t>(0u);
  auto best_metric = std::numeric_limits<double>::infinity();
  const auto sample_count = std::min<std::size_t>(child.dots.size(), 16u);

  for (std::size_t sample = 0; sample < sample_count; ++sample) {
    const auto candidate_index =
        static_cast<std::size_t>(random_.next_u32() % child.dots.size());
    const auto& current_dot = child.dots[candidate_index];
    const auto distance =
        std::hypot(current_dot.x - proposal.x, current_dot.y - proposal.y);
    if (distance <= current_dot.radius + proposal.radius + 0.5) {
      return candidate_index;
    }

    const auto metric = dot_target_score(current_dot) + distance * 0.05;
    if (metric < best_metric) {
      best_metric = metric;
      best_index = candidate_index;
    }
  }

  return best_index;
}

/**
 * Runs a lightweight hill-climbing pass on a candidate by repeatedly
 * reworking a few weak dots with local or guided proposals.
 */
void Optimizer::refine_candidate(Candidate* candidate, std::uint32_t attempts) {
  if (candidate == nullptr || candidate->dots.empty()) {
    return;
  }

  for (std::uint32_t attempt = 0; attempt < attempts; ++attempt) {
    auto index = static_cast<std::size_t>(random_.next_u32() % candidate->dots.size());
    for (std::uint32_t probe = 0; probe < 3; ++probe) {
      const auto probe_index =
          static_cast<std::size_t>(random_.next_u32() % candidate->dots.size());
      if (dot_target_score(candidate->dots[probe_index]) <
          dot_target_score(candidate->dots[index])) {
        index = probe_index;
      }
    }

    const auto current_dot = candidate->dots[index];
    const auto proposal =
        random_.next_unit() < 0.7
            ? local_search_dot(current_dot, mutation_distance_scale() * 0.6, 0.10)
            : guided_dot();
    if (dots_equal(current_dot, proposal)) {
      continue;
    }

    const auto next_error = candidate->grid.apply_dot_delta_and_update_error(
        current_dot, proposal, target_, candidate->squared_error);
    if (next_error < candidate->squared_error ||
        dot_target_score(proposal) > dot_target_score(current_dot) * 1.05) {
      candidate->squared_error = next_error;
      candidate->dots[index] = proposal;
    } else {
      (void)candidate->grid.apply_dot_delta_and_update_error(
          proposal, current_dot, target_, next_error);
    }
  }

  update_candidate_fitness(*candidate);
}

/**
 * Mutates a candidate with a mix of local search, guided reseeding, and random
 * reseeding. The mix shifts toward broader exploration as stagnation rises.
 */
void Optimizer::mutate(Candidate& candidate) {
  const auto mutation_rate = adaptive_mutation_rate();
  const auto distance_scale = mutation_distance_scale();
  const auto radius_scale =
      0.12 + std::min(0.35, static_cast<double>(stagnation_generations_) * 0.015);

  for (auto& dot : candidate.dots) {
    if (random_.next_unit() >= mutation_rate) {
      continue;
    }

    Dot next_dot = dot;
    const auto mutation_mode = random_.next_unit();
    if (mutation_mode < 0.55) {
      next_dot = local_search_dot(dot, distance_scale, radius_scale);
    } else if (mutation_mode < 0.85 && total_target_weight_ > 0.0) {
      next_dot = guided_dot();
    } else {
      next_dot = random_dot();
    }

    if (dots_equal(dot, next_dot)) {
      continue;
    }

    const auto next_error = candidate.grid.apply_dot_delta_and_update_error(
        dot, next_dot, target_, candidate.squared_error);
    const auto current_score = dot_target_score(dot);
    const auto next_score = dot_target_score(next_dot);
    const auto accept_exploration =
        random_.next_unit() <
        0.04 + std::min(0.12, static_cast<double>(stagnation_generations_) * 0.01);

    if (next_error <= candidate.squared_error || next_score > current_score * 1.05 ||
        accept_exploration) {
      candidate.squared_error = next_error;
      dot = next_dot;
    } else {
      (void)candidate.grid.apply_dot_delta_and_update_error(
          next_dot, dot, target_, next_error);
    }
  }
}

}  // namespace stippling
