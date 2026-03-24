#pragma once

// Plain C ABI over the native stippling engine. This surface is consumed by the
// generated WASM module and can also be used from non-C++ callers.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StipplingEngine StipplingEngine;

/* Pixel formats accepted by the C ABI image-view helpers. */
typedef enum StipplingPixelFormat {
  STIPPLING_PIXEL_FORMAT_GRAYSCALE8 = 0,
  STIPPLING_PIXEL_FORMAT_RGBA8 = 1,
} StipplingPixelFormat;

/* Engine configuration used when starting optimization. */
typedef struct StipplingEngineConfig {
  uint32_t population_size;
  double mutation_rate;
  uint32_t dot_count;
  double elitism_ratio;
  uint32_t seed;
  uint32_t generations_per_batch;
} StipplingEngineConfig;

/* Target-preparation configuration used during preprocessing. */
typedef struct StipplingTargetProcessingConfig {
  uint32_t blur_amount;
  uint32_t threshold;
  uint32_t max_dot_count;
} StipplingTargetProcessingConfig;

/* Summary statistics for the prepared target image. */
typedef struct StipplingTargetStats {
  uint32_t black_pixels;
  uint32_t total_pixels;
  double black_percentage;
  uint32_t recommended_dot_count;
} StipplingTargetStats;

/* Serialized dot geometry returned by snapshot/export helpers. */
typedef struct StipplingDot {
  double x;
  double y;
  double radius;
} StipplingDot;

/* Whole-run optimizer progress returned after batch stepping. */
typedef struct StipplingOptimizerProgress {
  uint32_t generation;
  double best_fitness;
  uint64_t best_squared_error;
} StipplingOptimizerProgress;

/* Validation output for incremental raster bookkeeping. */
typedef struct StipplingOptimizerValidation {
  int valid;
  uint32_t checked_candidates;
  uint32_t mismatched_candidates;
  uint32_t first_mismatch_index;
  uint64_t max_squared_error_delta;
  uint32_t total_pixel_mismatches;
} StipplingOptimizerValidation;

/* Non-owning image view passed into target-preparation calls. */
typedef struct StipplingImageBufferView {
  int width;
  int height;
  StipplingPixelFormat format;
  const uint8_t* pixels;
  size_t length;
} StipplingImageBufferView;

/* Creates and destroys engine handles. */
StipplingEngine* stippling_engine_create(void);
void stippling_engine_destroy(StipplingEngine* engine);

/* Configures the optimizer. */
int stippling_engine_configure(StipplingEngine* engine,
                               const StipplingEngineConfig* config);
int stippling_engine_configure_values(StipplingEngine* engine,
                                      uint32_t population_size,
                                      double mutation_rate,
                                      uint32_t dot_count,
                                      double elitism_ratio,
                                      uint32_t seed,
                                      uint32_t generations_per_batch);
/* Prepares a target image from a generic image view or raw rgba8 pixels. */
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
/* Initializes and advances the optimizer. */
int stippling_engine_initialize_optimizer(StipplingEngine* engine);
int stippling_engine_evolve_batch(StipplingEngine* engine,
                                  StipplingOptimizerProgress* progress);
int stippling_engine_evolve_batch_in_place(StipplingEngine* engine);
/* Accesses the prepared preview image. */
int stippling_engine_prepared_image_width(const StipplingEngine* engine);
int stippling_engine_prepared_image_height(const StipplingEngine* engine);
size_t stippling_engine_prepared_image_byte_length(const StipplingEngine* engine);
size_t stippling_engine_copy_prepared_image_rgba8(const StipplingEngine* engine,
                                                  uint8_t* output,
                                                  size_t capacity);
/* Accesses the current best-dot snapshot and exports. */
size_t stippling_engine_best_dot_count(const StipplingEngine* engine);
size_t stippling_engine_copy_best_dots(const StipplingEngine* engine,
                                       StipplingDot* output,
                                       size_t capacity);
size_t stippling_engine_best_svg_byte_length(const StipplingEngine* engine,
                                             int scale);
size_t stippling_engine_copy_best_svg(const StipplingEngine* engine,
                                      char* output,
                                      size_t capacity,
                                      int scale);
size_t stippling_engine_best_png_byte_length(const StipplingEngine* engine,
                                             int scale);
size_t stippling_engine_copy_best_png(const StipplingEngine* engine,
                                      uint8_t* output,
                                      size_t capacity,
                                      int scale);

/* Accesses target and optimizer metrics. */
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
StipplingOptimizerValidation stippling_engine_validate_optimizer(
    const StipplingEngine* engine);
/* Returns the last stored error string for a handle. */
const char* stippling_engine_last_error(const StipplingEngine* engine);

#ifdef __cplusplus
}
#endif
