#pragma once

// C ABI over the engine, used by the WASM build. Functions returning `int`
// report 0 on success and -1 on failure, with the message available from
// `stippling_engine_last_error()`.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StipplingEngine StipplingEngine;

/* Layout is relied on by the WASM bridge: three consecutive f64 values. */
typedef struct StipplingDot {
  double x;
  double y;
  double radius;
} StipplingDot;

typedef struct StipplingOptimizerValidation {
  int valid;
  uint32_t checked_candidates;
  uint32_t mismatched_candidates;
  uint32_t first_mismatch_index;
  uint64_t max_squared_error_delta;
  uint32_t total_pixel_mismatches;
} StipplingOptimizerValidation;

typedef enum StipplingExportFormat {
  STIPPLING_EXPORT_SVG = 0,
  STIPPLING_EXPORT_PNG = 1,
  STIPPLING_EXPORT_TIMELAPSE_SVG = 2,
} StipplingExportFormat;

StipplingEngine* stippling_engine_create(void);
void stippling_engine_destroy(StipplingEngine* engine);

int stippling_engine_configure(StipplingEngine* engine,
                               uint32_t population_size,
                               double mutation_rate,
                               uint32_t dot_count,
                               double elitism_ratio,
                               uint32_t seed,
                               uint32_t generations_per_batch,
                               uint32_t thread_count);
int stippling_engine_prepare_target_rgba8(StipplingEngine* engine,
                                          int width,
                                          int height,
                                          const uint8_t* pixels,
                                          size_t length,
                                          uint32_t blur_amount,
                                          uint32_t threshold,
                                          uint32_t max_dot_count);
int stippling_engine_initialize_optimizer(StipplingEngine* engine);
int stippling_engine_evolve_batch(StipplingEngine* engine);

/* The prepared image is the thresholded black/white preview. */
int stippling_engine_prepared_image_width(const StipplingEngine* engine);
int stippling_engine_prepared_image_height(const StipplingEngine* engine);
size_t stippling_engine_prepared_image_byte_length(const StipplingEngine* engine);
size_t stippling_engine_copy_prepared_image_rgba8(const StipplingEngine* engine,
                                                  uint8_t* output,
                                                  size_t capacity);

uint32_t stippling_engine_target_black_pixels(const StipplingEngine* engine);
uint32_t stippling_engine_target_total_pixels(const StipplingEngine* engine);
double stippling_engine_target_black_percentage(const StipplingEngine* engine);
uint32_t stippling_engine_target_recommended_dot_count(const StipplingEngine* engine);

/* The dot buffer belongs to the handle and stays valid until the next capture. */
int stippling_engine_capture_best_dots(StipplingEngine* engine);
const StipplingDot* stippling_engine_best_dots_data(const StipplingEngine* engine);
size_t stippling_engine_best_dots_count(const StipplingEngine* engine);

/*
 * The export buffer belongs to the handle and stays valid until the next
 * export. `frame_duration_ms` only applies to the timelapse.
 */
int stippling_engine_export(StipplingEngine* engine,
                            StipplingExportFormat format,
                            int scale,
                            uint32_t frame_duration_ms);
const uint8_t* stippling_engine_export_data(const StipplingEngine* engine);
size_t stippling_engine_export_size(const StipplingEngine* engine);

/* Each returns 0 before the optimizer is initialized. */
uint32_t stippling_engine_optimizer_generation(const StipplingEngine* engine);
double stippling_engine_optimizer_best_fitness(const StipplingEngine* engine);
uint64_t stippling_engine_optimizer_best_squared_error(const StipplingEngine* engine);
StipplingOptimizerValidation stippling_engine_validate_optimizer(
    const StipplingEngine* engine);

const char* stippling_engine_last_error(const StipplingEngine* engine);

#ifdef __cplusplus
}
#endif
