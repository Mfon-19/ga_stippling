#pragma once

#include <cstdint>
#include <vector>

#include "stippling/engine/dot.hpp"
#include "stippling/engine/engine.hpp"

namespace stippling {

/**
 * Deterministic native optimizer that mirrors the current browser GA at a high
 * level while living entirely in the C++ core.
 *
 * The first version still recomputes candidate fitness from full redraws, but
 * it is structured around the raster-grid abstraction so incremental fitness
 * can replace those redraws later without changing the engine surface.
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
    std::vector<Dot> dots{};
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

  void ensure_initialized() const;
  void initialize_population();
  void evaluate_population();
  void evaluate_candidate(Candidate& candidate) const;
  std::vector<Candidate> preserve_elites(std::uint32_t elite_count) const;
  Candidate make_child(const Candidate& parent_a, const Candidate& parent_b);
  const Candidate& select_parent();
  double dot_target_score(const Dot& dot) const;
  Dot random_dot();
  void mutate(Candidate& candidate);
};

}  // namespace stippling
