#include "stippling/engine/engine.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  stippling::Engine engine;

  assert(engine.status() == stippling::EngineStatus::idle);
  assert(!engine.has_image());

  stippling::EngineConfig config{};
  config.seed = 42;
  config.dot_count = 2048;
  engine.configure(config);

  assert(engine.config().seed == 42);
  assert(engine.config().dot_count == 2048);
  assert(engine.status() == stippling::EngineStatus::configured);

  stippling::ImageBuffer image{
      .width = 2,
      .height = 2,
      .pixels = std::vector<std::uint8_t>{0, 32, 128, 255},
  };
  engine.load_image(std::move(image));

  assert(engine.has_image());
  assert(engine.image().pixels.size() == 4);
  assert(engine.status() == stippling::EngineStatus::image_loaded);

  return 0;
}
