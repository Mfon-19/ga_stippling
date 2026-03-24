#include "stippling/engine/c_api.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  StipplingEngine* engine = stippling_engine_create();
  assert(engine != nullptr);

  const StipplingEngineConfig engine_config{
      .population_size = 100,
      .mutation_rate = 0.2,
      .dot_count = 2048,
      .elitism_ratio = 0.7,
      .seed = 42,
      .generations_per_batch = 1,
  };
  assert(stippling_engine_configure(engine, &engine_config) == 0);
  assert(stippling_engine_configure_values(engine,
                                           engine_config.population_size,
                                           engine_config.mutation_rate,
                                           engine_config.dot_count,
                                           engine_config.elitism_ratio,
                                           engine_config.seed,
                                           engine_config.generations_per_batch) == 0);

  const std::vector<std::uint8_t> rgba_pixels{
      0,   0,   0,   255, 255, 255, 255, 255,
      128, 128, 128, 255, 32,  32,  32,  255,
  };
  const StipplingImageBufferView image{
      .width = 2,
      .height = 2,
      .format = STIPPLING_PIXEL_FORMAT_RGBA8,
      .pixels = rgba_pixels.data(),
      .length = rgba_pixels.size(),
  };
  const StipplingTargetProcessingConfig processing{
      .blur_amount = 0,
      .threshold = 130,
      .max_dot_count = 200000,
  };

  assert(stippling_engine_prepare_target(engine, &image, &processing) == 0);
  assert(stippling_engine_prepare_target_rgba8(engine,
                                               image.width,
                                               image.height,
                                               image.pixels,
                                               image.length,
                                               processing.blur_amount,
                                               processing.threshold,
                                               processing.max_dot_count) == 0);

  const StipplingTargetStats stats = stippling_engine_target_stats(engine);
  assert(stats.black_pixels == 3);
  assert(stats.total_pixels == 4);
  assert(stats.recommended_dot_count == 1);
  assert(stippling_engine_target_black_pixels(engine) == 3);
  assert(stippling_engine_target_total_pixels(engine) == 4);
  assert(stippling_engine_target_recommended_dot_count(engine) == 1);
  assert(stippling_engine_prepared_image_width(engine) == 2);
  assert(stippling_engine_prepared_image_height(engine) == 2);
  assert(stippling_engine_prepared_image_byte_length(engine) == rgba_pixels.size());
  std::vector<std::uint8_t> prepared_pixels(
      stippling_engine_prepared_image_byte_length(engine));
  assert(stippling_engine_copy_prepared_image_rgba8(
             engine, prepared_pixels.data(), prepared_pixels.size()) ==
         prepared_pixels.size());
  assert(prepared_pixels[0] == 0);
  assert(prepared_pixels[4] == 255);

  assert(stippling_engine_initialize_optimizer(engine) == 0);
  const StipplingOptimizerProgress initial_progress =
      stippling_engine_optimizer_progress(engine);
  assert(initial_progress.generation == 0);

  StipplingOptimizerProgress evolved{};
  assert(stippling_engine_evolve_batch(engine, &evolved) == 0);
  assert(evolved.generation == 1);
  assert(stippling_engine_evolve_batch_in_place(engine) == 0);
  assert(stippling_engine_optimizer_generation(engine) == 2);
  assert(evolved.best_squared_error >= 0);
  assert(stippling_engine_optimizer_best_fitness(engine) >= 0.0);
  const size_t best_dot_count = stippling_engine_best_dot_count(engine);
  assert(best_dot_count == 2048);
  std::vector<StipplingDot> best_dots(best_dot_count);
  assert(stippling_engine_copy_best_dots(engine, best_dots.data(), best_dots.size()) ==
         best_dot_count);
  assert(best_dots.front().radius > 0.0);

  stippling_engine_destroy(engine);
  return 0;
}
