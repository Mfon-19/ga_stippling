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

  const StipplingTargetStats stats = stippling_engine_target_stats(engine);
  assert(stats.black_pixels == 3);
  assert(stats.total_pixels == 4);
  assert(stats.recommended_dot_count == 150000);

  stippling_engine_destroy(engine);
  return 0;
}
