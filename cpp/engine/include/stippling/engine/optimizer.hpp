#pragma once

#include <cstdint>
#include <vector>

#include "stippling/engine/dot.hpp"
#include "stippling/engine/engine.hpp"
#include "stippling/engine/raster_grid.hpp"
#include "stippling/engine/worker_pool.hpp"

namespace stippling {

/**
 * Genetic search at one pyramid level. Deliberately a hybrid rather than a
 * textbook GA: importance-weighted seeding, island tournaments, local search on
 * elites, and partial restarts when the population stalls.
 *
 * Must stay deterministic: native and WASM builds are compared bit-for-bit.
 */
class Optimizer {
 public:
  Optimizer(int width,
            int height,
            std::vector<std::uint8_t> target,
            std::vector<double> importance,
            const EngineConfig& config);
  Optimizer(int width,
            int height,
            std::vector<std::uint8_t> target,
            std::vector<double> importance,
            const EngineConfig& config,
            std::vector<Dot> seed_dots);

  /** Breeds children on `pool` when set; results don't depend on thread count. */
  void set_worker_pool(WorkerPool* pool) noexcept;
  void initialize();
  OptimizerProgress evolve_batch();

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const std::vector<Dot>& best_dots() const;
  [[nodiscard]] OptimizerProgress progress() const noexcept;
  [[nodiscard]] OptimizerValidation validate_incremental_state() const;
  [[nodiscard]] bool ready_to_promote_for_multiscale() const noexcept;

 private:
  struct Candidate {
    // Owns its raster so a one-dot change can be scored without a full redraw.
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
  std::vector<double> importance_;
  std::vector<Dot> seed_dots_{};
  std::vector<Candidate> population_{};
  OptimizerProgress progress_{};
  // Serial steps only; each child uses its own generator (see evolve_batch).
  RandomGenerator random_;
  WorkerPool* pool_{nullptr};
  std::vector<double> cumulative_target_weights_{};
  // sampler_guide_[k] is where the search for bucket k's weights starts; see
  // sample_target_index().
  std::vector<std::uint32_t> sampler_guide_{};
  double total_target_weight_{0.0};
  // dot_target_score() per pixel, precomputed so scoring is a single load.
  std::vector<double> target_scores_{};
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
  void apply_restart_strategy_if_needed();
  std::vector<Candidate> preserve_elites(std::uint32_t elite_count) const;
  void refine_elites(std::vector<Candidate>* elites);
  void migrate_islands();
  double sampler_bucket_start(std::size_t bucket) const;
  double adaptive_mutation_rate() const;
  double mutation_distance_scale() const;
  double dot_target_score(const Dot& dot) const;

  // Everything a child needs is const apart from the generator it's handed, so
  // children can be bred concurrently.
  Candidate make_child(RandomGenerator& rng,
                       const Candidate& parent_a,
                       const Candidate& parent_b) const;
  const Candidate& select_parent(RandomGenerator& rng, std::size_t island_index) const;
  std::size_t sample_target_index(RandomGenerator& rng) const;
  Dot guided_dot(RandomGenerator& rng) const;
  Dot random_dot(RandomGenerator& rng) const;
  Dot local_search_dot(RandomGenerator& rng,
                       const Dot& dot,
                       double distance_scale,
                       double radius_scale) const;
  std::size_t find_replacement_index(RandomGenerator& rng,
                                     const Candidate& child,
                                     const Dot& proposal) const;
  void refine_candidate(RandomGenerator& rng,
                        Candidate* candidate,
                        std::uint32_t attempts) const;
  void mutate(RandomGenerator& rng, Candidate& candidate) const;
};

}  // namespace stippling
