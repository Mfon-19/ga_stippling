#include "stippling/engine/engine.hpp"

#include "check.hpp"
#include <cstdint>
#include <vector>

namespace {

stippling::ImageBuffer make_source_image() {
  constexpr int kWidth = 16;
  constexpr int kHeight = 16;
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(kWidth * kHeight * 4), 255u);

  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto dark_pixel =
          x == y || x + y == kWidth - 1 ||
          (x >= 5 && x <= 10 && y >= 4 && y <= 11) ||
          (y == 8 && x > 2 && x < 13);
      const auto value = static_cast<std::uint8_t>(dark_pixel ? 16 : 240);
      const auto index =
          static_cast<std::size_t>((y * kWidth + x) * 4);
      pixels[index] = value;
      pixels[index + 1] = value;
      pixels[index + 2] = value;
      pixels[index + 3] = 255u;
    }
  }

  return {
      .format = stippling::PixelFormat::rgba8,
      .width = kWidth,
      .height = kHeight,
      .pixels = std::move(pixels),
  };
}

stippling::EngineConfig make_engine_config() {
  return {
      .population_size = 12,
      .mutation_rate = 0.2,
      .dot_count = 24,
      .elitism_ratio = 0.2,
      .seed = 1337,
      .generations_per_batch = 1,
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
  CHECK(initial_left.generation == 0);
  CHECK(initial_right.generation == 0);
  CHECK(initial_left.best_fitness == initial_right.best_fitness);
  CHECK(initial_left.best_squared_error == initial_right.best_squared_error);
  CHECK(left.capabilities().multiscale);
  CHECK(right.capabilities().multiscale);

  for (std::uint32_t generation = 1; generation <= 6; ++generation) {
    const auto progressed_left = left.evolve_batch();
    const auto progressed_right = right.evolve_batch();

    CHECK(progressed_left.generation == generation);
    CHECK(progressed_right.generation == generation);
    CHECK(progressed_left.best_fitness == progressed_right.best_fitness);
    CHECK(progressed_left.best_squared_error == progressed_right.best_squared_error);
  }

  CHECK(left.validate_optimizer().valid);
  CHECK(right.validate_optimizer().valid);
  CHECK(left.best_dots().size() == right.best_dots().size());
  CHECK(!left.best_dots().empty());
  CHECK(left.best_dots().front().x == right.best_dots().front().x);
  CHECK(left.best_dots().front().y == right.best_dots().front().y);
  CHECK(left.best_dots().front().radius == right.best_dots().front().radius);

  return 0;
}
