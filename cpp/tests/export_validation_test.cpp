#include "stippling/engine/c_api.h"
#include "stippling/engine/engine.hpp"

#include "check.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

stippling::ImageBuffer make_source_image() {
  return {
      .format = stippling::PixelFormat::rgba8,
      .width = 4,
      .height = 4,
      .pixels = std::vector<std::uint8_t>{
          0,   0,   0,   255, 32,  32,  32,  255, 255, 255, 255, 255, 255, 255, 255, 255,
          0,   0,   0,   255, 32,  32,  32,  255, 255, 255, 255, 255, 255, 255, 255, 255,
          255, 255, 255, 255, 32,  32,  32,  255, 0,   0,   0,   255, 255, 255, 255, 255,
          255, 255, 255, 255, 32,  32,  32,  255, 0,   0,   0,   255, 255, 255, 255, 255,
      },
  };
}

stippling::EngineConfig make_config() {
  return {
      .population_size = 10,
      .mutation_rate = 0.2,
      .dot_count = 12,
      .elitism_ratio = 0.2,
      .seed = 7,
      .generations_per_batch = 2,
  };
}

std::uint32_t read_png_dimension(const std::vector<std::uint8_t>& png,
                                 std::size_t offset) {
  return (static_cast<std::uint32_t>(png[offset]) << 24u) |
         (static_cast<std::uint32_t>(png[offset + 1]) << 16u) |
         (static_cast<std::uint32_t>(png[offset + 2]) << 8u) |
         static_cast<std::uint32_t>(png[offset + 3]);
}

}  // namespace

int main() {
  stippling::Engine engine;
  engine.configure(make_config());
  (void)engine.prepare_target(make_source_image(), {});
  engine.initialize_optimizer();
  (void)engine.evolve_batch();

  const auto validation = engine.validate_optimizer();
  CHECK(validation.valid);
  CHECK(validation.checked_candidates == make_config().population_size);
  CHECK(validation.mismatched_candidates == 0);

  const auto svg = engine.export_best_svg(2);
  CHECK(svg.find("<svg") != std::string::npos);
  CHECK(svg.find("<circle") != std::string::npos);

  const auto png = engine.export_best_png(2);
  CHECK(png.size() > 32);
  CHECK(std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
  CHECK(read_png_dimension(png, 16) == 8);
  CHECK(read_png_dimension(png, 20) == 8);

  StipplingEngine* c_engine = stippling_engine_create();
  CHECK(c_engine != nullptr);
  const auto config = make_config();
  CHECK(stippling_engine_configure_values(c_engine,
                                           config.population_size,
                                           config.mutation_rate,
                                           config.dot_count,
                                           config.elitism_ratio,
                                           config.seed,
                                           config.generations_per_batch) == 0);
  const auto source_image = make_source_image();
  CHECK(stippling_engine_prepare_target_rgba8(
             c_engine,
             source_image.width,
             source_image.height,
             source_image.pixels.data(),
             source_image.pixels.size(),
             0,
             130,
             200000) == 0);
  CHECK(stippling_engine_initialize_optimizer(c_engine) == 0);
  CHECK(stippling_engine_evolve_batch_in_place(c_engine) == 0);

  const auto c_validation = stippling_engine_validate_optimizer(c_engine);
  CHECK(c_validation.valid == 1);
  CHECK(c_validation.mismatched_candidates == 0);
  const auto svg_size = stippling_engine_best_svg_byte_length(c_engine, 1);
  std::string c_svg(svg_size, '\0');
  CHECK(stippling_engine_copy_best_svg(c_engine,
                                        c_svg.data(),
                                        c_svg.size(),
                                        1) == c_svg.size());
  CHECK(c_svg.find("<svg") != std::string::npos);
  const auto png_size = stippling_engine_best_png_byte_length(c_engine, 1);
  std::vector<std::uint8_t> c_png(png_size);
  CHECK(stippling_engine_copy_best_png(c_engine,
                                        c_png.data(),
                                        c_png.size(),
                                        1) == c_png.size());
  CHECK(c_png.size() > 32);
  CHECK(std::memcmp(c_png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);

  stippling_engine_destroy(c_engine);
  return 0;
}
