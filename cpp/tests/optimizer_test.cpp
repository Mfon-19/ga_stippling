#include "stippling/engine/engine.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

stippling::ImageBuffer make_source_image() {
  return {
      .format = stippling::PixelFormat::rgba8,
      .width = 3,
      .height = 3,
      .pixels = std::vector<std::uint8_t>{
          0,   0,   0,   255, 255, 255, 255, 255, 64,  64,  64,  255,
          255, 255, 255, 255, 32,  32,  32,  255, 255, 255, 255, 255,
          0,   0,   0,   255, 255, 255, 255, 255, 96,  96,  96,  255,
      },
  };
}

stippling::EngineConfig make_engine_config() {
  return {
      .population_size = 12,
      .mutation_rate = 0.2,
      .dot_count = 16,
      .elitism_ratio = 0.5,
      .seed = 1337,
      .generations_per_batch = 2,
  };
}

}  // namespace

int main() {
  stippling::Engine left;
  stippling::Engine right;

  left.configure(make_engine_config());
  right.configure(make_engine_config());

  (void)left.prepare_target(make_source_image(), {});
  (void)right.prepare_target(make_source_image(), {});

  left.initialize_optimizer();
  right.initialize_optimizer();

  const auto initial_left = left.optimizer_progress();
  const auto initial_right = right.optimizer_progress();
  assert(initial_left.generation == 0);
  assert(initial_right.generation == 0);
  assert(initial_left.best_fitness == initial_right.best_fitness);
  assert(initial_left.best_squared_error == initial_right.best_squared_error);

  const auto progressed_left = left.evolve_batch();
  const auto progressed_right = right.evolve_batch();

  assert(progressed_left.generation == 2);
  assert(progressed_right.generation == 2);
  assert(progressed_left.best_fitness == progressed_right.best_fitness);
  assert(progressed_left.best_squared_error == progressed_right.best_squared_error);
  assert(left.best_dots().size() == right.best_dots().size());
  assert(!left.best_dots().empty());
  assert(left.best_dots().front().x == right.best_dots().front().x);
  assert(left.best_dots().front().y == right.best_dots().front().y);
  assert(left.best_dots().front().radius == right.best_dots().front().radius);

  return 0;
}
