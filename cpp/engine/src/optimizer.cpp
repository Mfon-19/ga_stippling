#include "stippling/engine/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "stippling/engine/raster_grid.hpp"

namespace stippling {

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
}

void Optimizer::initialize() {
  initialize_population();
  evaluate_population();
  progress_.generation = 0;
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
    evaluate_population();
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

void Optimizer::initialize_population() {
  population_.clear();
  population_.reserve(config_.population_size);

  for (std::uint32_t candidate_index = 0;
       candidate_index < config_.population_size; ++candidate_index) {
    Candidate candidate;
    candidate.dots.reserve(config_.dot_count);

    for (std::uint32_t dot_index = 0; dot_index < config_.dot_count; ++dot_index) {
      candidate.dots.push_back(random_dot());
    }

    population_.push_back(std::move(candidate));
  }
}

void Optimizer::evaluate_population() {
  for (auto& candidate : population_) {
    evaluate_candidate(candidate);
  }

  const auto best = std::max_element(
      population_.begin(), population_.end(),
      [](const Candidate& left, const Candidate& right) {
        return left.fitness < right.fitness;
      });
  progress_.best_fitness = best->fitness;
  progress_.best_squared_error = best->squared_error;
}

void Optimizer::evaluate_candidate(Candidate& candidate) const {
  RasterGrid grid(width_, height_);
  for (const auto& dot : candidate.dots) {
    grid.draw_dot(dot);
  }

  candidate.squared_error = grid.squared_error(target_);
  const auto max_diff = static_cast<double>(width_) * static_cast<double>(height_) *
                        255.0 * 255.0;
  const auto raw_fitness =
      1.0 - static_cast<double>(candidate.squared_error) / max_diff;
  candidate.fitness = std::sqrt(std::max(0.0, raw_fitness));
}

std::vector<Optimizer::Candidate> Optimizer::preserve_elites(
    std::uint32_t elite_count) const {
  auto sorted = population_;
  std::sort(sorted.begin(), sorted.end(),
            [](const Candidate& left, const Candidate& right) {
              return left.fitness > right.fitness;
            });
  sorted.resize(std::min<std::size_t>(elite_count, sorted.size()));
  return sorted;
}

Optimizer::Candidate Optimizer::make_child(const Candidate& parent_a,
                                           const Candidate& parent_b) {
  Candidate child;
  child.dots.reserve(config_.dot_count);

  for (std::size_t index = 0; index < parent_a.dots.size(); ++index) {
    const auto& selected_parent =
        dot_target_score(parent_a.dots[index]) > dot_target_score(parent_b.dots[index])
            ? parent_a
            : parent_b;
    child.dots.push_back(selected_parent.dots[index]);
  }

  mutate(child);
  return child;
}

const Optimizer::Candidate& Optimizer::select_parent() {
  const auto total_fitness = std::accumulate(
      population_.begin(), population_.end(), 0.0,
      [](double sum, const Candidate& candidate) {
        return sum + candidate.fitness;
      });

  if (total_fitness <= 0.0) {
    const auto random_index = static_cast<std::size_t>(
        std::floor(random_.next_unit() * static_cast<double>(population_.size())));
    return population_[std::min(random_index, population_.size() - 1)];
  }

  auto threshold = random_.next_unit() * total_fitness;
  double running_sum = 0.0;

  for (const auto& candidate : population_) {
    running_sum += candidate.fitness;
    if (running_sum >= threshold) {
      return candidate;
    }
  }

  return population_.back();
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
      .radius = 1.0 + random_.next_unit() * 2.0,
  };
}

void Optimizer::mutate(Candidate& candidate) {
  for (auto& dot : candidate.dots) {
    if (random_.next_unit() < config_.mutation_rate) {
      dot.x = std::floor(random_.next_unit() * static_cast<double>(width_));
    }
    if (random_.next_unit() < config_.mutation_rate) {
      dot.y = std::floor(random_.next_unit() * static_cast<double>(height_));
    }
    if (random_.next_unit() < config_.mutation_rate) {
      dot.radius = 1.0 + random_.next_unit() * 2.0;
    }
  }
}

}  // namespace stippling
