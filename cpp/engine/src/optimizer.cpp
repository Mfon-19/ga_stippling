#include "stippling/engine/optimizer.hpp"

// Genetic search at one pyramid level. The Engine (engine.cpp) owns the
// pyramid and decides when to promote to a finer level.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>

namespace stippling {

namespace {

bool dots_equal(const Dot& left, const Dot& right) {
  return left.x == right.x && left.y == right.y && left.radius == right.radius;
}

/**
 * Strict total order: fitness, then error, then dot geometry, so every build
 * picks the same champion even among exact ties.
 */
template <typename CandidateLike>
bool candidate_better(const CandidateLike& left, const CandidateLike& right) {
  constexpr double kFitnessEpsilon = 1e-12;

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

double clamp_position(double value, int limit) {
  return std::clamp(value, 0.0, static_cast<double>(std::max(0, limit - 1)));
}

std::size_t island_count_for_population(std::size_t population_size) {
  if (population_size >= 48) {
    return 4;
  }
  if (population_size >= 16) {
    return 2;
  }
  return 1;
}

/** splitmix64 finalizer: spreads nearby inputs across the whole 64-bit range. */
std::uint64_t mix_bits(std::uint64_t value) {
  value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31u);
}

/**
 * Seed for the child bred into `slot` this generation. Keyed by the run seed,
 * the level size (so pyramid levels differ), generation, and slot, so a child
 * depends only on the current population and its slot.
 */
std::uint32_t child_stream_seed(std::uint32_t run_seed,
                                int width,
                                int height,
                                std::uint32_t generation,
                                std::size_t slot) {
  auto state = mix_bits(run_seed);
  state = mix_bits(state ^ (static_cast<std::uint64_t>(width) << 32u) ^
                   static_cast<std::uint64_t>(height));
  state = mix_bits(state ^ generation);
  state = mix_bits(state ^ slot);
  return static_cast<std::uint32_t>(state ^ (state >> 32u));
}

}  // namespace

// mulberry32: tiny, fast, and the same in every build.
Optimizer::RandomGenerator::RandomGenerator(std::uint32_t seed)
    : state_(seed) {}

double Optimizer::RandomGenerator::next_unit() {
  state_ += 0x6d2b79f5u;
  auto t = state_;
  t = std::uint32_t((t ^ (t >> 15)) * (t | 1u));
  t ^= t + std::uint32_t((t ^ (t >> 7)) * (t | 61u));
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

std::uint32_t Optimizer::RandomGenerator::next_u32() {
  return static_cast<std::uint32_t>(next_unit() * 4294967295.0);
}

Optimizer::Optimizer(int width,
                     int height,
                     std::vector<std::uint8_t> target,
                     std::vector<double> importance,
                     const EngineConfig& config)
    : Optimizer(width, height, std::move(target), std::move(importance), config, {}) {}

/** `seed_dots` carry the best solution from the previous, coarser level. */
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

void Optimizer::initialize() {
  initialize_population();
  evaluate_population();
  progress_.generation = 0;
  last_best_fitness_ = progress_.best_fitness;
  stagnation_generations_ = 0;
}

/**
 * Each generation keeps the elites, hill-climbs the best few, breeds the rest,
 * then migrates between islands and restarts part of the population if stuck.
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
    const auto first_slot = next_population.size();
    const auto child_count =
        config_.population_size > first_slot ? config_.population_size - first_slot : 0u;
    std::vector<std::optional<Candidate>> children(child_count);

    const auto breed = [&](std::size_t offset) {
      const auto slot = first_slot + offset;
      RandomGenerator rng(child_stream_seed(config_.seed, width_, height_,
                                            progress_.generation, slot));
      const auto island_index = island_count == 1 ? 0u : slot % island_count;
      const auto& parent_a = select_parent(rng, island_index);
      const auto& parent_b = select_parent(rng, island_index);
      auto child = make_child(rng, parent_a, parent_b);
      if (rng.next_unit() < 0.25) {
        refine_candidate(rng, &child, 2);
      }
      children[offset].emplace(std::move(child));
    };
    if (pool_ != nullptr) {
      pool_->run(child_count, breed);
    } else {
      for (std::size_t offset = 0; offset < child_count; ++offset) {
        breed(offset);
      }
    }
    for (auto& child : children) {
      next_population.push_back(std::move(*child));
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

void Optimizer::set_worker_pool(WorkerPool* pool) noexcept {
  pool_ = pool;
}

bool Optimizer::initialized() const noexcept {
  return !population_.empty();
}

const std::vector<Dot>& Optimizer::best_dots() const {
  ensure_initialized();
  const auto best = std::max_element(
      population_.begin(), population_.end(),
      [](const Candidate& left, const Candidate& right) {
        return candidate_better(right, left);
      });
  return best->dots;
}

OptimizerProgress Optimizer::progress() const noexcept {
  return progress_;
}

/** Redraws every candidate from scratch and compares it with its incremental state. */
OptimizerValidation Optimizer::validate_incremental_state() const {
  ensure_initialized();

  OptimizerValidation validation{};
  validation.checked_candidates =
      static_cast<std::uint32_t>(population_.size());
  validation.first_mismatch_index = validation.checked_candidates;

  for (std::size_t candidate_index = 0; candidate_index < population_.size();
       ++candidate_index) {
    const auto& candidate = population_[candidate_index];
    RasterGrid full(width_, height_);

    for (const auto& dot : candidate.dots) {
      full.draw_dot(dot);
    }

    const auto recomputed_error = full.squared_error(target_);
    const auto coverage_match = full.coverage() == candidate.grid.coverage();
    const auto error_match = recomputed_error == candidate.squared_error;

    if (coverage_match && error_match) {
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

    const auto expected_pixels = full.pixels();
    const auto actual_pixels = candidate.grid.pixels();
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

bool Optimizer::ready_to_promote_for_multiscale() const noexcept {
  if (!initialized()) {
    return false;
  }

  return progress_.generation >= 2 &&
         (stagnation_generations_ >= 1 || progress_.best_fitness >= 0.92 ||
          progress_.generation >= 6);
}

void Optimizer::ensure_initialized() const {
  if (!initialized()) {
    throw std::logic_error("Optimizer population has not been initialized");
  }
}

// Cumulative weights so guided dots favor dark pixels, with extra pull
// toward edges.
void Optimizer::build_target_sampler() {
  cumulative_target_weights_.clear();
  cumulative_target_weights_.reserve(target_.size());
  total_target_weight_ = 0.0;
  target_scores_.resize(target_.size());

  for (std::size_t index = 0; index < target_.size(); ++index) {
    const auto darkness = (255.0 - static_cast<double>(target_[index])) / 255.0;
    const auto importance = index < importance_.size() ? importance_[index] : 0.0;
    const auto weight = std::max(0.0, darkness * 0.65 + importance * 0.35);
    total_target_weight_ += weight;
    cumulative_target_weights_.push_back(total_target_weight_);

    // Same expression dot_target_score() used to evaluate, so identical bits.
    const auto score_darkness = 255.0 - static_cast<double>(target_[index]);
    const auto score_importance = importance_[index] * 255.0;
    target_scores_[index] = score_darkness * 0.65 + score_importance * 0.35;
  }

  // One bucket per pixel on average keeps each bucket's search to a few steps.
  const auto bucket_count = cumulative_target_weights_.size();
  sampler_guide_.resize(bucket_count + 1u);
  std::size_t position = 0;
  for (std::size_t bucket = 0; bucket <= bucket_count; ++bucket) {
    const auto start = bucket < bucket_count ? sampler_bucket_start(bucket)
                                             : total_target_weight_;
    while (position < cumulative_target_weights_.size() &&
           cumulative_target_weights_[position] <= start) {
      ++position;
    }
    sampler_guide_[bucket] = static_cast<std::uint32_t>(position);
  }
}

double Optimizer::sampler_bucket_start(std::size_t bucket) const {
  return total_target_weight_ *
         (static_cast<double>(bucket) /
          static_cast<double>(cumulative_target_weights_.size()));
}

/**
 * With seed dots, candidate 0 keeps them exactly and the others jitter around
 * them, restoring diversity after a promotion.
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
                                           .radius = clamp_dot_radius(seed_dot.radius),
                                       }
                                     : local_search_dot(random_, seed_dot, 1.35, 0.10));
        continue;
      }

      const auto use_guided_seed =
          total_target_weight_ > 0.0 && random_.next_unit() < 0.9;
      candidate.dots.push_back(use_guided_seed ? guided_dot(random_) : random_dot(random_));
    }

    population_.push_back(std::move(candidate));
  }
}

void Optimizer::evaluate_population() {
  for (auto& candidate : population_) {
    evaluate_candidate(candidate);
  }

  refresh_progress();
}

void Optimizer::evaluate_candidate(Candidate& candidate) const {
  candidate.grid.clear();
  for (const auto& dot : candidate.dots) {
    candidate.grid.draw_dot(dot);
  }

  candidate.squared_error = candidate.grid.squared_error(target_);
  update_candidate_fitness(candidate);
}

// The sqrt spreads out scores near 1 so late-run progress stays visible.
void Optimizer::update_candidate_fitness(Candidate& candidate) const {
  const auto max_diff = static_cast<double>(width_) * static_cast<double>(height_) *
                        255.0 * 255.0;
  const auto raw_fitness =
      1.0 - static_cast<double>(candidate.squared_error) / max_diff;
  candidate.fitness = std::sqrt(std::max(0.0, raw_fitness));
}

void Optimizer::refresh_progress() {
  const auto best = std::max_element(
      population_.begin(), population_.end(),
      [](const Candidate& left, const Candidate& right) {
        return candidate_better(right, left);
      });
  progress_.best_fitness = best->fitness;
  progress_.best_squared_error = best->squared_error;
}

void Optimizer::update_search_state() {
  constexpr double kImprovementEpsilon = 1e-6;

  if (progress_.best_fitness > last_best_fitness_ + kImprovementEpsilon) {
    last_best_fitness_ = progress_.best_fitness;
    stagnation_generations_ = 0;
    return;
  }

  ++stagnation_generations_;
}

/** After a long stall, replaces the weakest ~20% with reseeds around the champion. */
void Optimizer::apply_restart_strategy_if_needed() {
  const auto restart_threshold = width_ < 96 ? 8u : 10u;
  if (stagnation_generations_ < restart_threshold || population_.size() < 4) {
    return;
  }

  std::vector<std::size_t> sorted_indices(population_.size(), 0u);
  std::iota(sorted_indices.begin(), sorted_indices.end(), 0u);
  std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](std::size_t left, std::size_t right) {
              return candidate_better(population_[left], population_[right]);
            });

  // Copy just the dots; copying the candidate would drag its raster along.
  const auto champion_dots = population_[sorted_indices.front()].dots;
  const auto restart_count =
      std::max<std::size_t>(1u, population_.size() / 5u);

  for (std::size_t offset = 0; offset < restart_count; ++offset) {
    Candidate replacement(width_, height_);
    replacement.dots.reserve(config_.dot_count);

    const auto champion_seed_count = std::min<std::size_t>(
        champion_dots.size(), std::max<std::size_t>(1u, config_.dot_count / 4u));
    for (std::size_t seed_index = 0; seed_index < champion_seed_count; ++seed_index) {
      replacement.dots.push_back(
          local_search_dot(random_, champion_dots[seed_index], 2.5, 0.18));
    }
    while (replacement.dots.size() < config_.dot_count) {
      replacement.dots.push_back(random_.next_unit() < 0.8 ? guided_dot(random_)
                                                           : random_dot(random_));
    }

    evaluate_candidate(replacement);
    population_[sorted_indices[sorted_indices.size() - 1u - offset]] =
        std::move(replacement);
  }

  stagnation_generations_ /= 2u;
  refresh_progress();
  last_best_fitness_ = progress_.best_fitness;
}

// Sorts indices, not candidates, so only the kept elites copy their rasters.
std::vector<Optimizer::Candidate> Optimizer::preserve_elites(
    std::uint32_t elite_count) const {
  std::vector<std::size_t> order(population_.size(), 0u);
  std::iota(order.begin(), order.end(), 0u);
  const auto keep_count = std::min<std::size_t>(elite_count, order.size());
  std::partial_sort(order.begin(), order.begin() + static_cast<long>(keep_count),
                    order.end(), [&](std::size_t left, std::size_t right) {
                      return candidate_better(population_[left], population_[right]);
                    });

  std::vector<Candidate> elites;
  elites.reserve(config_.population_size);
  for (std::size_t rank = 0; rank < keep_count; ++rank) {
    elites.push_back(population_[order[rank]]);
  }
  return elites;
}

void Optimizer::refine_elites(std::vector<Candidate>* elites) {
  if (elites == nullptr || elites->empty()) {
    return;
  }

  const auto refinement_count = std::min<std::size_t>(3u, elites->size());
  for (std::size_t index = 0; index < refinement_count; ++index) {
    refine_candidate(random_, &(*elites)[index], 5u + index * 2u);
  }
}

/**
 * Starts from the fitter parent and imports dots from the other one. Dots have
 * no identity, so each import replaces whichever child dot it most likely helps.
 */
Optimizer::Candidate Optimizer::make_child(RandomGenerator& rng,
                                           const Candidate& parent_a,
                                           const Candidate& parent_b) const {
  const auto& primary_parent =
      parent_a.fitness >= parent_b.fitness ? parent_a : parent_b;
  const auto& secondary_parent =
      parent_a.fitness >= parent_b.fitness ? parent_b : parent_a;
  Candidate child = primary_parent;
  const auto import_attempts = std::min<std::size_t>(
      std::max<std::size_t>(6u, config_.dot_count / 12u), 24u);

  for (std::size_t attempt = 0; attempt < import_attempts; ++attempt) {
    const auto secondary_index = static_cast<std::size_t>(
        rng.next_u32() % secondary_parent.dots.size());
    Dot proposal = secondary_parent.dots[secondary_index];

    if (rng.next_unit() < 0.4) {
      const auto anchor_index =
          static_cast<std::size_t>(rng.next_u32() % primary_parent.dots.size());
      const auto& anchor_dot = primary_parent.dots[anchor_index];
      proposal.x = clamp_position((proposal.x + anchor_dot.x) * 0.5, width_);
      proposal.y = clamp_position((proposal.y + anchor_dot.y) * 0.5, height_);
      proposal.radius = clamp_dot_radius((proposal.radius + anchor_dot.radius) * 0.5);
    } else if (rng.next_unit() < 0.65) {
      proposal = local_search_dot(rng, proposal, mutation_distance_scale() * 0.7, 0.08);
    }

    const auto replacement_index = find_replacement_index(rng, child, proposal);
    const auto current_dot = child.dots[replacement_index];
    const auto current_score = dot_target_score(current_dot);
    const auto proposal_score = dot_target_score(proposal);
    if (proposal_score + 4.0 < current_score && rng.next_unit() < 0.9) {
      continue;
    }

    const auto next_error = child.grid.apply_dot_delta_and_update_error(
        current_dot, proposal, target_, child.squared_error);
    const auto accept =
        next_error <= child.squared_error ||
        proposal_score > current_score * 1.08 ||
        rng.next_unit() <
            0.06 + std::min(0.1, stagnation_generations_ * 0.01);
    if (accept) {
      child.squared_error = next_error;
      child.dots[replacement_index] = proposal;
    } else {
      (void)child.grid.apply_dot_delta_and_update_error(
          proposal, current_dot, target_, next_error);
    }
  }

  mutate(rng, child);
  update_candidate_fitness(child);
  return child;
}

/**
 * Tournament selection, mostly within one island. Global sampling grows with
 * stagnation so breakthroughs can spread across the population.
 */
const Optimizer::Candidate& Optimizer::select_parent(RandomGenerator& rng,
                                                      std::size_t island_index) const {
  constexpr std::size_t kTournamentSize = 4;
  const auto island_count = island_count_for_population(population_.size());
  const auto island = island_count == 0 ? 0u : island_index % island_count;
  const auto island_start = island * population_.size() / island_count;
  const auto island_end = (island + 1u) * population_.size() / island_count;
  const auto sample_global = island_count == 1 ||
                             rng.next_unit() <
                                 0.12 + std::min(0.1, stagnation_generations_ * 0.01);

  auto sample_index = [&]() -> std::size_t {
    if (sample_global) {
      return static_cast<std::size_t>(rng.next_u32() % population_.size());
    }

    const auto span = std::max<std::size_t>(1u, island_end - island_start);
    return island_start + static_cast<std::size_t>(rng.next_u32() % span);
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

/** Every 4 generations, each island's champion replaces the next island's weakest. */
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

std::size_t Optimizer::sample_target_index(RandomGenerator& rng) const {
  if (total_target_weight_ <= 0.0 || cumulative_target_weights_.empty()) {
    return static_cast<std::size_t>(rng.next_u32() % target_.size());
  }

  const auto threshold = rng.next_unit() * total_target_weight_;

  // Equivalent to upper_bound over all weights, but searches one bucket.
  // bucket_start(k) <= threshold < bucket_start(k + 1) guarantees the answer
  // lies in [guide[k], guide[k + 1]], so the result is exactly the same.
  const auto bucket_count = cumulative_target_weights_.size();
  auto bucket = std::min(
      bucket_count - 1u,
      static_cast<std::size_t>(threshold / total_target_weight_ *
                               static_cast<double>(bucket_count)));
  while (bucket > 0 && threshold < sampler_bucket_start(bucket)) {
    --bucket;
  }
  while (bucket + 1u < bucket_count && threshold >= sampler_bucket_start(bucket + 1u)) {
    ++bucket;
  }
  const auto first = cumulative_target_weights_.begin() + sampler_guide_[bucket];
  const auto last =
      cumulative_target_weights_.begin() +
      static_cast<long>(std::min<std::size_t>(bucket_count, sampler_guide_[bucket + 1u] + 1u));
  const auto match = std::upper_bound(first, last, threshold);
  if (match == cumulative_target_weights_.end()) {
    return cumulative_target_weights_.size() - 1u;
  }

  return static_cast<std::size_t>(
      std::distance(cumulative_target_weights_.begin(), match));
}

double Optimizer::adaptive_mutation_rate() const {
  const auto multiplier =
      1.0 + std::min(2.0, static_cast<double>(stagnation_generations_) * 0.08);
  return std::min(0.75, config_.mutation_rate * multiplier);
}

double Optimizer::mutation_distance_scale() const {
  return 1.2 + std::min(6.0, static_cast<double>(stagnation_generations_) * 0.45);
}

// Jitter lets many dots spread through an important region instead of stacking.
Dot Optimizer::guided_dot(RandomGenerator& rng) const {
  const auto target_index = sample_target_index(rng);
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
          base_x + (rng.next_unit() * 2.0 - 1.0) * jitter_scale, width_),
      .y = clamp_position(
          base_y + (rng.next_unit() * 2.0 - 1.0) * jitter_scale, height_),
      .radius =
          clamp_dot_radius(0.4 + darkness * 0.35 + importance * 0.25 +
                       rng.next_unit() * 0.18),
  };
}

double Optimizer::dot_target_score(const Dot& dot) const {
  const auto x = static_cast<int>(std::floor(dot.x));
  const auto y = static_cast<int>(std::floor(dot.y));

  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    return 0.0;
  }

  return target_scores_[static_cast<std::size_t>(y * width_ + x)];
}

Dot Optimizer::random_dot(RandomGenerator& rng) const {
  return {
      .x = std::floor(rng.next_unit() * static_cast<double>(width_)),
      .y = std::floor(rng.next_unit() * static_cast<double>(height_)),
      .radius = 0.4 + rng.next_unit() * 0.55,
  };
}

/** Returns the best of a few nearby samples by target score (not raster error). */
Dot Optimizer::local_search_dot(RandomGenerator& rng,
                                const Dot& dot,
                                double distance_scale,
                                double radius_scale) const {
  auto best_dot = Dot{
      .x = clamp_position(dot.x, width_),
      .y = clamp_position(dot.y, height_),
      .radius = clamp_dot_radius(dot.radius),
  };
  auto best_score = dot_target_score(best_dot);

  const auto sample_count = 6u;
  for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
    const auto offset_x = (rng.next_unit() * 2.0 - 1.0) * distance_scale;
    const auto offset_y = (rng.next_unit() * 2.0 - 1.0) * distance_scale;
    const auto proposal = Dot{
        .x = clamp_position(best_dot.x + offset_x, width_),
        .y = clamp_position(best_dot.y + offset_y, height_),
        .radius = clamp_dot_radius(
            best_dot.radius + (rng.next_unit() * 2.0 - 1.0) * radius_scale),
    };
    const auto proposal_score = dot_target_score(proposal);
    if (proposal_score > best_score) {
      best_dot = proposal;
      best_score = proposal_score;
    }
  }

  return best_dot;
}

/** Prefers a dot overlapping the proposal, otherwise a weak one nearby. */
std::size_t Optimizer::find_replacement_index(RandomGenerator& rng,
                                              const Candidate& child,
                                              const Dot& proposal) const {
  if (child.dots.empty()) {
    return 0u;
  }

  auto best_index = static_cast<std::size_t>(0u);
  auto best_metric = std::numeric_limits<double>::infinity();
  const auto sample_count = std::min<std::size_t>(child.dots.size(), 16u);

  for (std::size_t sample = 0; sample < sample_count; ++sample) {
    const auto candidate_index =
        static_cast<std::size_t>(rng.next_u32() % child.dots.size());
    const auto& current_dot = child.dots[candidate_index];
    // sqrt is correctly rounded everywhere; std::hypot is not, and would let
    // native and WASM builds disagree about which dot to replace.
    const auto dx = current_dot.x - proposal.x;
    const auto dy = current_dot.y - proposal.y;
    const auto distance = std::sqrt(dx * dx + dy * dy);
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

/** Hill-climbs a few weak dots with local or guided replacements. */
void Optimizer::refine_candidate(RandomGenerator& rng,
                                 Candidate* candidate,
                                 std::uint32_t attempts) const {
  if (candidate == nullptr || candidate->dots.empty()) {
    return;
  }

  for (std::uint32_t attempt = 0; attempt < attempts; ++attempt) {
    auto index = static_cast<std::size_t>(rng.next_u32() % candidate->dots.size());
    for (std::uint32_t probe = 0; probe < 3; ++probe) {
      const auto probe_index =
          static_cast<std::size_t>(rng.next_u32() % candidate->dots.size());
      if (dot_target_score(candidate->dots[probe_index]) <
          dot_target_score(candidate->dots[index])) {
        index = probe_index;
      }
    }

    const auto current_dot = candidate->dots[index];
    const auto proposal =
        rng.next_unit() < 0.7
            ? local_search_dot(rng, current_dot, mutation_distance_scale() * 0.6, 0.10)
            : guided_dot(rng);
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
 * Mixes local moves, guided reseeds, and random reseeds. Rate, step size, and
 * the chance of accepting a worse move all grow with stagnation.
 */
void Optimizer::mutate(RandomGenerator& rng, Candidate& candidate) const {
  const auto mutation_rate = adaptive_mutation_rate();
  const auto distance_scale = mutation_distance_scale();
  const auto radius_scale =
      0.12 + std::min(0.35, static_cast<double>(stagnation_generations_) * 0.015);

  for (auto& dot : candidate.dots) {
    if (rng.next_unit() >= mutation_rate) {
      continue;
    }

    Dot next_dot = dot;
    const auto mutation_mode = rng.next_unit();
    if (mutation_mode < 0.55) {
      next_dot = local_search_dot(rng, dot, distance_scale, radius_scale);
    } else if (mutation_mode < 0.85 && total_target_weight_ > 0.0) {
      next_dot = guided_dot(rng);
    } else {
      next_dot = random_dot(rng);
    }

    if (dots_equal(dot, next_dot)) {
      continue;
    }

    const auto next_error = candidate.grid.apply_dot_delta_and_update_error(
        dot, next_dot, target_, candidate.squared_error);
    const auto current_score = dot_target_score(dot);
    const auto next_score = dot_target_score(next_dot);
    const auto accept_exploration =
        rng.next_unit() <
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
