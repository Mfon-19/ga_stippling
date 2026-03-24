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

typedef struct StipplingDot {
  double x;
  double y;
  double radius;
} StipplingDot;

typedef struct StipplingOptimizerProgress {
  uint32_t generation;
  double best_fitness;
  uint64_t best_squared_error;
} StipplingOptimizerProgress;

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
int stippling_engine_configure_values(StipplingEngine* engine,
                                      uint32_t population_size,
                                      double mutation_rate,
                                      uint32_t dot_count,
                                      double elitism_ratio,
                                      uint32_t seed,
                                      uint32_t generations_per_batch);
int stippling_engine_prepare_target(
    StipplingEngine* engine,
    const StipplingImageBufferView* source_image,
    const StipplingTargetProcessingConfig* config);
int stippling_engine_prepare_target_rgba8(StipplingEngine* engine,
                                          int width,
                                          int height,
                                          const uint8_t* pixels,
                                          size_t length,
                                          uint32_t blur_amount,
                                          uint32_t threshold,
                                          uint32_t max_dot_count);
int stippling_engine_initialize_optimizer(StipplingEngine* engine);
int stippling_engine_evolve_batch(StipplingEngine* engine,
                                  StipplingOptimizerProgress* progress);
int stippling_engine_evolve_batch_in_place(StipplingEngine* engine);
int stippling_engine_prepared_image_width(const StipplingEngine* engine);
int stippling_engine_prepared_image_height(const StipplingEngine* engine);
size_t stippling_engine_prepared_image_byte_length(const StipplingEngine* engine);
size_t stippling_engine_copy_prepared_image_rgba8(const StipplingEngine* engine,
                                                  uint8_t* output,
                                                  size_t capacity);
size_t stippling_engine_best_dot_count(const StipplingEngine* engine);
size_t stippling_engine_copy_best_dots(const StipplingEngine* engine,
                                       StipplingDot* output,
                                       size_t capacity);

StipplingTargetStats stippling_engine_target_stats(const StipplingEngine* engine);
uint32_t stippling_engine_target_black_pixels(const StipplingEngine* engine);
uint32_t stippling_engine_target_total_pixels(const StipplingEngine* engine);
double stippling_engine_target_black_percentage(const StipplingEngine* engine);
uint32_t stippling_engine_target_recommended_dot_count(const StipplingEngine* engine);
StipplingOptimizerProgress stippling_engine_optimizer_progress(
    const StipplingEngine* engine);
uint32_t stippling_engine_optimizer_generation(const StipplingEngine* engine);
double stippling_engine_optimizer_best_fitness(const StipplingEngine* engine);
uint64_t stippling_engine_optimizer_best_squared_error(const StipplingEngine* engine);
const char* stippling_engine_last_error(const StipplingEngine* engine);

#ifdef __cplusplus
}
#endif
