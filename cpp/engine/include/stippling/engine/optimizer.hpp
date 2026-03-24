#pragma once

#include <cstdint>
#include <vector>

#include "stippling/engine/dot.hpp"
#include "stippling/engine/engine.hpp"
#include "stippling/engine/raster_grid.hpp"

namespace stippling {

/**
 * Deterministic native optimizer that mirrors the current browser GA at a high
 * level while living entirely in the C++ core.
 *
 * Candidates own their raster state so child construction and mutation can
 * update squared error incrementally instead of redrawing the entire image
 * after every small dot change.
 */
class Optimizer {
 public:
  Optimizer(int width,
            int height,
            std::vector<std::uint8_t> target,
            const EngineConfig& config);

  void initialize();
  OptimizerProgress evolve_batch();

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const std::vector<Dot>& best_dots() const;
  [[nodiscard]] OptimizerProgress progress() const noexcept;

 private:
  struct Candidate {
    explicit Candidate(int width, int height) : grid(width, height) {}

    std::vector<Dot> dots{};
    RasterGrid grid;
    double fitness{0.0};
    std::uint64_t squared_error{0};
  };

  class RandomGenerator {
   public:
    explicit RandomGenerator(std::uint32_t seed);

    [[nodiscard]] double next_unit();
    [[nodiscard]] std::uint32_t next_u32();

   private:
    std::uint32_t state_;
  };

  int width_;
  int height_;
  EngineConfig config_;
  std::vector<std::uint8_t> target_;
  std::vector<Candidate> population_{};
  OptimizerProgress progress_{};
  mutable RandomGenerator random_;
  std::vector<double> cumulative_target_weights_{};
  double total_target_weight_{0.0};
  double last_best_fitness_{0.0};
  std::uint32_t stagnation_generations_{0};

  void ensure_initialized() const;
  void build_target_sampler();
  void initialize_population();
  void evaluate_population();
  void evaluate_candidate(Candidate& candidate) const;
  void update_candidate_fitness(Candidate& candidate) const;
  void refresh_progress();
  void update_search_state();
  std::vector<Candidate> preserve_elites(std::uint32_t elite_count) const;
  Candidate make_child(const Candidate& parent_a, const Candidate& parent_b);
  const Candidate& select_parent();
  std::size_t sample_target_index();
  double adaptive_mutation_rate() const;
  double mutation_distance_scale() const;
  Dot guided_dot();
  double dot_target_score(const Dot& dot) const;
  Dot random_dot();
  void mutate(Candidate& candidate);
};

}  // namespace stippling
