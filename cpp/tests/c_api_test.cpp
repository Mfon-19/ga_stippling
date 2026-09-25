#include "stippling/engine/c_api.h"

#include "check.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

int main() {
  StipplingEngine* engine = stippling_engine_create();
  CHECK(engine != nullptr);

  CHECK(stippling_engine_configure(engine, 100, 0.2, 2048, 0.7, 42, 1, 2) == 0);

  const std::vector<std::uint8_t> rgba_pixels{
      0,   0,   0,   255, 255, 255, 255, 255,
      128, 128, 128, 255, 32,  32,  32,  255,
  };

  // Engine errors come back as -1 plus a message instead of escaping the ABI.
  CHECK(stippling_engine_prepare_target_rgba8(engine, 2, 2, rgba_pixels.data(),
                                              rgba_pixels.size() - 1, 0, 130,
                                              200000) == -1);
  CHECK(std::string(stippling_engine_last_error(engine)).find("byte length") !=
        std::string::npos);

  CHECK(stippling_engine_prepare_target_rgba8(engine, 2, 2, rgba_pixels.data(),
                                              rgba_pixels.size(), 0, 130,
                                              200000) == 0);
  CHECK(std::string(stippling_engine_last_error(engine)).empty());

  CHECK(stippling_engine_target_black_pixels(engine) == 3);
  CHECK(stippling_engine_target_total_pixels(engine) == 4);
  CHECK(stippling_engine_target_recommended_dot_count(engine) == 1);
  CHECK(stippling_engine_prepared_image_width(engine) == 2);
  CHECK(stippling_engine_prepared_image_height(engine) == 2);
  CHECK(stippling_engine_prepared_image_byte_length(engine) == rgba_pixels.size());
  std::vector<std::uint8_t> prepared_pixels(
      stippling_engine_prepared_image_byte_length(engine));
  CHECK(stippling_engine_copy_prepared_image_rgba8(
            engine, prepared_pixels.data(), prepared_pixels.size()) ==
        prepared_pixels.size());
  CHECK(prepared_pixels[0] == 0);
  CHECK(prepared_pixels[4] == 255);

  // Progress getters, captures, and exports must not throw before initialization.
  CHECK(stippling_engine_optimizer_generation(engine) == 0);
  CHECK(stippling_engine_optimizer_best_fitness(engine) == 0.0);
  CHECK(stippling_engine_optimizer_best_squared_error(engine) == 0);
  CHECK(stippling_engine_capture_best_dots(engine) == -1);
  CHECK(stippling_engine_export(engine, STIPPLING_EXPORT_SVG, 1, 0) == -1);

  CHECK(stippling_engine_initialize_optimizer(engine) == 0);
  CHECK(stippling_engine_optimizer_generation(engine) == 0);
  CHECK(stippling_engine_evolve_batch(engine) == 0);
  CHECK(stippling_engine_evolve_batch(engine) == 0);
  CHECK(stippling_engine_optimizer_generation(engine) == 2);
  CHECK(stippling_engine_optimizer_best_fitness(engine) >= 0.0);

  CHECK(stippling_engine_capture_best_dots(engine) == 0);
  CHECK(stippling_engine_best_dots_count(engine) == 2048);
  CHECK(stippling_engine_best_dots_data(engine)[0].radius > 0.0);

  // Exports render once into a handle-owned buffer.
  CHECK(stippling_engine_export(engine, STIPPLING_EXPORT_SVG, 1, 0) == 0);
  const std::string svg(
      reinterpret_cast<const char*>(stippling_engine_export_data(engine)),
      stippling_engine_export_size(engine));
  CHECK(svg.starts_with("<svg"));
  CHECK(svg.ends_with("</svg>"));

  CHECK(stippling_engine_export(engine, STIPPLING_EXPORT_PNG, 2, 0) == 0);
  CHECK(stippling_engine_export_size(engine) > 32);
  CHECK(std::memcmp(stippling_engine_export_data(engine), "\x89PNG\r\n\x1a\n", 8) == 0);

  CHECK(stippling_engine_export(engine, STIPPLING_EXPORT_TIMELAPSE_SVG, 1, 100) == 0);
  CHECK(stippling_engine_export_size(engine) > 0);

  // Invalid arguments report an error and leave the previous artifact intact.
  const auto previous_size = stippling_engine_export_size(engine);
  CHECK(stippling_engine_export(engine, STIPPLING_EXPORT_SVG, 0, 0) == -1);
  CHECK(std::string(stippling_engine_last_error(engine)).find("scale") !=
        std::string::npos);
  CHECK(stippling_engine_export_size(engine) == previous_size);

  const auto validation = stippling_engine_validate_optimizer(engine);
  CHECK(validation.valid == 1);

  stippling_engine_destroy(engine);
  return 0;
}
