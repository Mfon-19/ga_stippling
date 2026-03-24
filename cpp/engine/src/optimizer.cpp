#include "stippling/engine/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace stippling {

namespace {

bool dots_equal(const Dot& left, const Dot& right) {
  return left.x == right.x && left.y == right.y && left.radius == right.radius;
}

double clamp_position(double value, int limit) {
  return std::clamp(value, 0.0, static_cast<double>(std::max(0, limit - 1)));
}

double clamp_radius(double value) {
  return std::clamp(value, 0.45, 1.75);
}

}  // namespace

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
                     const EngineConfig& config)
    : width_(width),
      height_(height),
      config_(config),
      target_(std::move(target)),
      random_(config.seed) {
  if (width_ <= 0 || height_ <= 0) {
    throw std::invalid_argument("Optimizer dimensions must be positive");
  }
  if (target_.size() != static_cast<std::size_t>(width_ * height_)) {
    throw std::invalid_argument("Optimizer target size does not match dimensions");
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

OptimizerProgress Optimizer::evolve_batch() {
  ensure_initialized();

  for (std::uint32_t batch_index = 0; batch_index < config_.generations_per_batch;
       ++batch_index) {
    const auto elite_count = std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::floor(
                static_cast<double>(config_.population_size) *
                config_.elitism_ratio)));
    auto next_population = preserve_elites(elite_count);

    while (next_population.size() < config_.population_size) {
      const auto& parent_a = select_parent();
      const auto& parent_b = select_parent();
      next_population.push_back(make_child(parent_a, parent_b));
    }

    population_ = std::move(next_population);
    refresh_progress();
    update_search_state();
    ++progress_.generation;
  }

  return progress_;
}

bool Optimizer::initialized() const noexcept {
  return !population_.empty();
}

const std::vector<Dot>& Optimizer::best_dots() const {
  ensure_initialized();
  const auto best = std::max_element(
      population_.begin(), population_.end(),
      [](const Candidate& left, const Candidate& right) {
        return left.fitness < right.fitness;
      });
  return best->dots;
}

OptimizerProgress Optimizer::progress() const noexcept {
  return progress_;
}

void Optimizer::ensure_initialized() const {
  if (!initialized()) {
    throw std::logic_error("Optimizer population has not been initialized");
  }
}

void Optimizer::build_target_sampler() {
  cumulative_target_weights_.clear();
  cumulative_target_weights_.reserve(target_.size());
  total_target_weight_ = 0.0;

  for (const auto pixel : target_) {
    const auto darkness = 255.0 - static_cast<double>(pixel);
    total_target_weight_ += darkness;
    cumulative_target_weights_.push_back(total_target_weight_);
  }
}

void Optimizer::initialize_population() {
  population_.clear();
  population_.reserve(config_.population_size);

  for (std::uint32_t candidate_index = 0;
       candidate_index < config_.population_size; ++candidate_index) {
    Candidate candidate(width_, height_);
    candidate.dots.reserve(config_.dot_count);

    for (std::uint32_t dot_index = 0; dot_index < config_.dot_count; ++dot_index) {
      const auto use_guided_seed =
          total_target_weight_ > 0.0 && random_.next_unit() < 0.85;
      candidate.dots.push_back(use_guided_seed ? guided_dot() : random_dot());
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
        return left.fitness < right.fitness;
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

void Optimizer::evaluate_candidate(Candidate& candidate) const {
  candidate.grid.clear();
  for (const auto& dot : candidate.dots) {
    candidate.grid.draw_dot(dot);
  }

  candidate.squared_error = candidate.grid.squared_error(target_);
  update_candidate_fitness(candidate);
}

std::vector<Optimizer::Candidate> Optimizer::preserve_elites(
    std::uint32_t elite_count) const {
  auto sorted = population_;
  std::sort(sorted.begin(), sorted.end(),
            [](const Candidate& left, const Candidate& right) {
              return left.fitness > right.fitness;
            });
  const auto keep_count = std::min<std::size_t>(elite_count, sorted.size());
  sorted.erase(sorted.begin() + keep_count, sorted.end());
  return sorted;
}

Optimizer::Candidate Optimizer::make_child(const Candidate& parent_a,
                                           const Candidate& parent_b) {
  const auto& primary_parent =
      parent_a.fitness >= parent_b.fitness ? parent_a : parent_b;
  const auto& secondary_parent =
      parent_a.fitness >= parent_b.fitness ? parent_b : parent_a;
  Candidate child = primary_parent;
  const auto import_probability =
      std::min(0.45, 0.15 + static_cast<double>(stagnation_generations_) * 0.02);

  for (std::size_t index = 0; index < parent_a.dots.size(); ++index) {
    const auto& next_dot = secondary_parent.dots[index];
    const auto current_score = dot_target_score(child.dots[index]);
    const auto next_score = dot_target_score(next_dot);

    // Keep the fitter parent as the base genome, then opportunistically import
    // darker, better-placed dots from the secondary parent when they look more
    // promising at the target pixel they occupy.
    if (next_score > current_score && random_.next_unit() < import_probability &&
        !dots_equal(child.dots[index], next_dot)) {
      child.squared_error = child.grid.apply_dot_delta_and_update_error(
          child.dots[index], next_dot, target_, child.squared_error);
      child.dots[index] = next_dot;
    }
  }

  mutate(child);
  update_candidate_fitness(child);
  return child;
}

const Optimizer::Candidate& Optimizer::select_parent() {
  constexpr std::size_t kTournamentSize = 4;
  auto best_index = static_cast<std::size_t>(
      std::floor(random_.next_unit() * static_cast<double>(population_.size())));
  best_index = std::min(best_index, population_.size() - 1);

  for (std::size_t round = 1; round < kTournamentSize; ++round) {
    auto challenger_index = static_cast<std::size_t>(
        std::floor(random_.next_unit() * static_cast<double>(population_.size())));
    challenger_index = std::min(challenger_index, population_.size() - 1);

    if (population_[challenger_index].fitness > population_[best_index].fitness) {
      best_index = challenger_index;
    }
  }

  return population_[best_index];
}

std::size_t Optimizer::sample_target_index() {
  if (total_target_weight_ <= 0.0 || cumulative_target_weights_.empty()) {
    auto random_index = static_cast<std::size_t>(
        std::floor(random_.next_unit() * static_cast<double>(target_.size())));
    return std::min(random_index, target_.size() - 1);
  }

  const auto threshold = random_.next_unit() * total_target_weight_;
  const auto match = std::upper_bound(cumulative_target_weights_.begin(),
                                      cumulative_target_weights_.end(), threshold);
  if (match == cumulative_target_weights_.end()) {
    return cumulative_target_weights_.size() - 1;
  }

  return static_cast<std::size_t>(
      std::distance(cumulative_target_weights_.begin(), match));
}

double Optimizer::adaptive_mutation_rate() const {
  const auto multiplier =
      1.0 + std::min(2.5, static_cast<double>(stagnation_generations_) * 0.1);
  return std::min(0.85, config_.mutation_rate * multiplier);
}

double Optimizer::mutation_distance_scale() const {
  return 1.5 + std::min(8.0, static_cast<double>(stagnation_generations_) * 0.5);
}

Dot Optimizer::guided_dot() {
  const auto target_index = sample_target_index();
  const auto base_x =
      static_cast<double>(static_cast<int>(target_index % static_cast<std::size_t>(width_)));
  const auto base_y =
      static_cast<double>(static_cast<int>(target_index / static_cast<std::size_t>(width_)));
  const auto darkness =
      (255.0 - static_cast<double>(target_[target_index])) / 255.0;
  const auto jitter_scale = darkness > 0.0 ? 2.0 : 0.75;

  return {
      .x = clamp_position(
          base_x + (random_.next_unit() * 2.0 - 1.0) * jitter_scale, width_),
      .y = clamp_position(
          base_y + (random_.next_unit() * 2.0 - 1.0) * jitter_scale, height_),
      .radius = clamp_radius(0.55 + darkness * 0.55 + random_.next_unit() * 0.45),
  };
}

double Optimizer::dot_target_score(const Dot& dot) const {
  const auto x = static_cast<int>(std::floor(dot.x));
  const auto y = static_cast<int>(std::floor(dot.y));

  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    return 0.0;
  }

  const auto index = static_cast<std::size_t>(y * width_ + x);
  return 255.0 - static_cast<double>(target_[index]);
}

Dot Optimizer::random_dot() {
  return {
      .x = std::floor(random_.next_unit() * static_cast<double>(width_)),
      .y = std::floor(random_.next_unit() * static_cast<double>(height_)),
      .radius = 0.5 + random_.next_unit() * 0.9,
  };
}

void Optimizer::mutate(Candidate& candidate) {
  const auto mutation_rate = adaptive_mutation_rate();
  const auto distance_scale = mutation_distance_scale();
  const auto radius_scale =
      0.18 + std::min(0.55, static_cast<double>(stagnation_generations_) * 0.02);

  for (auto& dot : candidate.dots) {
    Dot next_dot = dot;
    bool changed = false;

    if (random_.next_unit() < mutation_rate) {
      const auto mutation_mode = random_.next_unit();

      if (mutation_mode < 0.6) {
        next_dot.x = clamp_position(
            dot.x + (random_.next_unit() * 2.0 - 1.0) * distance_scale, width_);
        next_dot.y = clamp_position(
            dot.y + (random_.next_unit() * 2.0 - 1.0) * distance_scale, height_);
        next_dot.radius = clamp_radius(
            dot.radius + (random_.next_unit() * 2.0 - 1.0) * radius_scale);
      } else if (mutation_mode < 0.85 && total_target_weight_ > 0.0) {
        next_dot = guided_dot();
      } else {
        next_dot = random_dot();
      }

      changed = !dots_equal(dot, next_dot);
    }

    if (changed) {
      candidate.squared_error = candidate.grid.apply_dot_delta_and_update_error(
          dot, next_dot, target_, candidate.squared_error);
      dot = next_dot;
    }
  }
}

}  // namespace stippling
