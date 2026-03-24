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
 *
 * The optimizer deliberately mixes several strategies instead of behaving like
 * a textbook GA:
 * - importance-weighted seeding to avoid spending early generations in empty
 *   space
 * - island-aware tournament selection to preserve some diversity
 * - local search on elites and promising proposals
 * - restart logic when the population stalls
 *
 * The determinism requirement is strict because the native CLI, WASM worker,
 * parity tests, and benchmark harness all compare outputs across runtimes.
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

  void initialize();
  OptimizerProgress evolve_batch();

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const std::vector<Dot>& best_dots() const;
  [[nodiscard]] OptimizerProgress progress() const noexcept;
  [[nodiscard]] OptimizerValidation validate_incremental_state() const;
  [[nodiscard]] bool ready_to_promote_for_multiscale() const noexcept;
  [[nodiscard]] std::uint32_t stagnation_generations() const noexcept;

 private:
  struct Candidate {
    // Each candidate owns its own incremental raster state. The grid tracks
    // coverage counts and rendered pixels so mutation/crossover can ask for the
    // exact error delta of replacing one dot without rebuilding the whole image.
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
  mutable RandomGenerator random_;
  std::vector<double> cumulative_target_weights_{};
  double total_target_weight_{0.0};
  double last_best_fitness_{0.0};
  std::uint32_t stagnation_generations_{0};

  // Build a cumulative distribution over target pixels so guided seeding and
  // guided mutation can sample dense / high-importance regions quickly.
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

  // Build a child by starting from the fitter parent and opportunistically
  // importing dots from the secondary parent when the incremental error delta
  // or target score says the replacement is worthwhile.
  Candidate make_child(const Candidate& parent_a, const Candidate& parent_b);

  // Parent selection is island-aware so most tournaments stay local while a
  // small global sample probability still allows migration-like mixing.
  const Candidate& select_parent(std::size_t island_index);
  void migrate_islands();
  std::size_t sample_target_index();
  double adaptive_mutation_rate() const;
  double mutation_distance_scale() const;
  Dot guided_dot();
  double dot_target_score(const Dot& dot) const;
  Dot random_dot();
  Dot local_search_dot(const Dot& dot, double distance_scale, double radius_scale);

  // Crossover does not preserve "dot identity". Instead it searches for a weak
  // or overlapping location in the child where a proposed dot is most likely to
  // improve local coverage.
  std::size_t find_replacement_index(const Candidate& child, const Dot& proposal) const;
  void refine_candidate(Candidate* candidate, std::uint32_t attempts);
  void mutate(Candidate& candidate);
};

}  // namespace stippling
