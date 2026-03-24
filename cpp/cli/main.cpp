#include "stippling/engine/engine.hpp"

#include <iostream>
#include <vector>

int main() {
  stippling::Engine engine;
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
  std::cout << "incremental_fitness="
            << engine.capabilities().incremental_fitness << '\n';
  std::cout << "multiscale=" << engine.capabilities().multiscale << '\n';

  return 0;
}
