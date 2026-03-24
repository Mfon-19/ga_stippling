#include "stippling/engine/engine.hpp"

#include <iostream>
#include <vector>

int main() {
  stippling::Engine engine;
  stippling::EngineConfig config{};
  config.population_size = 50;
  config.mutation_rate = 0.2;
  config.dot_count = 256;
  config.elitism_ratio = 0.5;
  config.seed = 1337;
  config.generations_per_batch = 1;
  engine.configure(config);
  const auto processed = engine.prepare_target(
      {
          .format = stippling::PixelFormat::rgba8,
          .width = 2,
          .height = 2,
          .pixels = std::vector<std::uint8_t>{
              0,   0,   0,   255, 255, 255, 255, 255,
              128, 128, 128, 255, 32,  32,  32,  255,
          },
      },
      {});

  std::cout << "stippling_cli backend status: " << engine.status_string()
            << '\n';
  std::cout << "prepared_pixels=" << processed.pixels.size() << '\n';
  std::cout << "recommended_dot_count="
            << engine.target_stats().recommended_dot_count << '\n';
  engine.initialize_optimizer();
  const auto progress = engine.evolve_batch();
  std::cout << "generation=" << progress.generation << '\n';
  std::cout << "best_fitness=" << progress.best_fitness << '\n';
  std::cout << "best_squared_error=" << progress.best_squared_error << '\n';
  std::cout << "best_dot_count=" << engine.best_dots().size() << '\n';
  std::cout << "incremental_fitness="
            << engine.capabilities().incremental_fitness << '\n';
  std::cout << "multiscale=" << engine.capabilities().multiscale << '\n';

  return 0;
}
