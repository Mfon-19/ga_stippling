#include "stippling/engine/engine.hpp"

#include "check.hpp"
#include <cstdint>
#include <vector>

int main() {
  stippling::Engine engine;

  CHECK(!engine.has_image());
  CHECK(!engine.has_optimizer());

  stippling::EngineConfig config{};
  config.seed = 42;
  config.dot_count = 2048;
  engine.configure(config);

  CHECK(engine.config().seed == 42);
  CHECK(engine.config().dot_count == 2048);

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
      {
          .blur_amount = 0,
          .threshold = 130,
          .max_dot_count = 200000,
      });

  CHECK(processed.valid());
  CHECK(processed.format == stippling::PixelFormat::rgba8);
  CHECK(engine.target_stats().black_pixels == 3);
  CHECK(engine.target_stats().total_pixels == 4);
  CHECK(engine.target_stats().recommended_dot_count == 1);

  CHECK(engine.has_image());
  CHECK(engine.image().pixels.size() == 16);

  return 0;
}
