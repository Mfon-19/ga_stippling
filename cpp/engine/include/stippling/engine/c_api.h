#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StipplingEngine StipplingEngine;

typedef enum StipplingPixelFormat {
  STIPPLING_PIXEL_FORMAT_GRAYSCALE8 = 0,
  STIPPLING_PIXEL_FORMAT_RGBA8 = 1,
} StipplingPixelFormat;

typedef struct StipplingEngineConfig {
  uint32_t population_size;
  double mutation_rate;
  uint32_t dot_count;
  double elitism_ratio;
  uint32_t seed;
  uint32_t generations_per_batch;
} StipplingEngineConfig;

typedef struct StipplingTargetProcessingConfig {
  uint32_t blur_amount;
  uint32_t threshold;
  uint32_t max_dot_count;
} StipplingTargetProcessingConfig;

typedef struct StipplingTargetStats {
  uint32_t black_pixels;
  uint32_t total_pixels;
  double black_percentage;
  uint32_t recommended_dot_count;
} StipplingTargetStats;

typedef struct StipplingImageBufferView {
  int width;
  int height;
  StipplingPixelFormat format;
  const uint8_t* pixels;
  size_t length;
} StipplingImageBufferView;

StipplingEngine* stippling_engine_create(void);
void stippling_engine_destroy(StipplingEngine* engine);

int stippling_engine_configure(StipplingEngine* engine,
                               const StipplingEngineConfig* config);
int stippling_engine_prepare_target(
    StipplingEngine* engine,
    const StipplingImageBufferView* source_image,
    const StipplingTargetProcessingConfig* config);

StipplingTargetStats stippling_engine_target_stats(const StipplingEngine* engine);
const char* stippling_engine_last_error(const StipplingEngine* engine);

#ifdef __cplusplus
}
#endif
