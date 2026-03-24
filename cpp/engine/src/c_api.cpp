#include "stippling/engine/c_api.h"

// c_api.cpp exposes the native engine through a plain C ABI.
//
// At a high level, this file is responsible for:
// - translating C-facing structs and enums into C++ engine types
// - wrapping engine calls in an exception-safe error boundary
// - copying prepared images, dot snapshots, metrics, and export artifacts
//   across the ABI boundary
//
// The WASM wrapper, browser worker, and native tests all depend on this layer
// rather than linking directly against C++ classes.

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "stippling/engine/engine.hpp"

struct StipplingEngine {
  stippling::Engine engine;
  std::string last_error{};
};

namespace {

/** Converts the C pixel-format enum into the native engine enum. */
stippling::PixelFormat to_cpp_format(StipplingPixelFormat format) {
  switch (format) {
    case STIPPLING_PIXEL_FORMAT_GRAYSCALE8:
      return stippling::PixelFormat::grayscale8;
    case STIPPLING_PIXEL_FORMAT_RGBA8:
      return stippling::PixelFormat::rgba8;
  }

  throw std::invalid_argument("Unknown pixel format");
}

/** Converts the C configuration struct into the native engine config type. */
stippling::EngineConfig to_cpp_config(const StipplingEngineConfig& config) {
  return {
      .population_size = config.population_size,
      .mutation_rate = config.mutation_rate,
      .dot_count = config.dot_count,
      .elitism_ratio = config.elitism_ratio,
      .seed = config.seed,
      .generations_per_batch = config.generations_per_batch,
  };
}

/** Converts the C preprocessing struct into the native processing config type. */
stippling::TargetProcessingConfig to_cpp_processing_config(
    const StipplingTargetProcessingConfig& config) {
  return {
      .blur_amount = config.blur_amount,
      .threshold = config.threshold,
      .max_dot_count = config.max_dot_count,
  };
}

/** Copies a C image view into an owning native image buffer. */
stippling::ImageBuffer to_cpp_image(const StipplingImageBufferView& image) {
  stippling::ImageBuffer buffer{
      .format = to_cpp_format(image.format),
      .width = image.width,
      .height = image.height,
      .pixels = std::vector<std::uint8_t>(image.pixels, image.pixels + image.length),
  };

  if (!buffer.valid()) {
    throw std::invalid_argument("ImageBufferView length does not match its format");
  }

  return buffer;
}

/** Converts native target stats into the C ABI struct. */
StipplingTargetStats to_c_stats(const stippling::TargetStats& stats) {
  return {
      .black_pixels = stats.black_pixels,
      .total_pixels = stats.total_pixels,
      .black_percentage = stats.black_percentage,
      .recommended_dot_count = stats.recommended_dot_count,
  };
}

/** Converts native progress metrics into the C ABI struct. */
StipplingOptimizerProgress to_c_progress(
    const stippling::OptimizerProgress& progress) {
  return {
      .generation = progress.generation,
      .best_fitness = progress.best_fitness,
      .best_squared_error = progress.best_squared_error,
  };
}

/** Converts native validation output into the C ABI struct. */
StipplingOptimizerValidation to_c_validation(
    const stippling::OptimizerValidation& validation) {
  return {
      .valid = validation.valid ? 1 : 0,
      .checked_candidates = validation.checked_candidates,
      .mismatched_candidates = validation.mismatched_candidates,
      .first_mismatch_index = validation.first_mismatch_index,
      .max_squared_error_delta = validation.max_squared_error_delta,
      .total_pixel_mismatches = validation.total_pixel_mismatches,
  };
}

/** Converts one native dot into its C ABI representation. */
StipplingDot to_c_dot(const stippling::Dot& dot) {
  return {
      .x = dot.x,
      .y = dot.y,
      .radius = dot.radius,
  };
}

/**
 * Runs one C ABI operation behind an exception boundary. Failures are stored on
 * the engine handle so callers can retrieve a stable error string.
 */
template <typename Callback>
int with_error_boundary(StipplingEngine* engine, Callback&& callback) {
  if (engine == nullptr) {
    return -1;
  }

  try {
    engine->last_error.clear();
    callback();
    return 0;
  } catch (const std::exception& error) {
    engine->last_error = error.what();
    return -1;
  }
}

}  // namespace

/** Allocates a new engine handle. */
StipplingEngine* stippling_engine_create(void) {
  return new StipplingEngine{};
}

/** Destroys an engine handle created by `stippling_engine_create()`. */
void stippling_engine_destroy(StipplingEngine* engine) {
  delete engine;
}

/** Applies a full engine configuration from the C ABI struct. */
int stippling_engine_configure(StipplingEngine* engine,
                               const StipplingEngineConfig* config) {
  if (config == nullptr) {
    return -1;
  }

  return with_error_boundary(engine, [&]() {
    engine->engine.configure(to_cpp_config(*config));
  });
}

/** Convenience overload that configures the engine from raw scalar values. */
int stippling_engine_configure_values(StipplingEngine* engine,
                                      uint32_t population_size,
                                      double mutation_rate,
                                      uint32_t dot_count,
                                      double elitism_ratio,
                                      uint32_t seed,
                                      uint32_t generations_per_batch) {
  const StipplingEngineConfig config{
      .population_size = population_size,
      .mutation_rate = mutation_rate,
      .dot_count = dot_count,
      .elitism_ratio = elitism_ratio,
      .seed = seed,
      .generations_per_batch = generations_per_batch,
  };
  return stippling_engine_configure(engine, &config);
}

/** Prepares the engine target from a generic C image view. */
int stippling_engine_prepare_target(
    StipplingEngine* engine,
    const StipplingImageBufferView* source_image,
    const StipplingTargetProcessingConfig* config) {
  if (source_image == nullptr || config == nullptr) {
    return -1;
  }

  return with_error_boundary(engine, [&]() {
    (void)engine->engine.prepare_target(to_cpp_image(*source_image),
                                        to_cpp_processing_config(*config));
  });
}

/** Convenience overload for preparing an rgba8 target directly from raw bytes. */
int stippling_engine_prepare_target_rgba8(StipplingEngine* engine,
                                          int width,
                                          int height,
                                          const uint8_t* pixels,
                                          size_t length,
                                          uint32_t blur_amount,
                                          uint32_t threshold,
                                          uint32_t max_dot_count) {
  if (pixels == nullptr) {
    return -1;
  }

  const StipplingImageBufferView image{
      .width = width,
      .height = height,
      .format = STIPPLING_PIXEL_FORMAT_RGBA8,
      .pixels = pixels,
      .length = length,
  };
  const StipplingTargetProcessingConfig config{
      .blur_amount = blur_amount,
      .threshold = threshold,
      .max_dot_count = max_dot_count,
  };
  return stippling_engine_prepare_target(engine, &image, &config);
}

/** Initializes the native optimizer for the current prepared target. */
int stippling_engine_initialize_optimizer(StipplingEngine* engine) {
  return with_error_boundary(engine, [&]() {
    engine->engine.initialize_optimizer();
  });
}

/** Advances the optimizer and writes progress into the supplied output struct. */
int stippling_engine_evolve_batch(StipplingEngine* engine,
                                  StipplingOptimizerProgress* progress) {
  if (progress == nullptr) {
    return -1;
  }

  return with_error_boundary(engine, [&]() {
    *progress = to_c_progress(engine->engine.evolve_batch());
  });
}

/** Advances the optimizer when the caller does not need the progress struct. */
int stippling_engine_evolve_batch_in_place(StipplingEngine* engine) {
  StipplingOptimizerProgress progress{};
  return stippling_engine_evolve_batch(engine, &progress);
}

/** Returns the prepared preview image width. */
int stippling_engine_prepared_image_width(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  return engine->engine.image().width;
}

/** Returns the prepared preview image height. */
int stippling_engine_prepared_image_height(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  return engine->engine.image().height;
}

/** Returns the prepared preview image byte length. */
size_t stippling_engine_prepared_image_byte_length(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  return engine->engine.image().pixels.size();
}

/** Copies the prepared preview image into a caller-owned RGBA buffer. */
size_t stippling_engine_copy_prepared_image_rgba8(const StipplingEngine* engine,
                                                  uint8_t* output,
                                                  size_t capacity) {
  if (engine == nullptr || output == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  const auto& pixels = engine->engine.image().pixels;
  const auto count = std::min<std::size_t>(capacity, pixels.size());
  std::copy_n(pixels.data(), count, output);
  return count;
}

/** Returns how many dots are in the current best solution. */
size_t stippling_engine_best_dot_count(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return 0;
  }

  return engine->engine.best_dots().size();
}

/** Copies the current best dots into a caller-owned array. */
size_t stippling_engine_copy_best_dots(const StipplingEngine* engine,
                                       StipplingDot* output,
                                       size_t capacity) {
  if (engine == nullptr || output == nullptr || !engine->engine.has_optimizer()) {
    return 0;
  }

  const auto& dots = engine->engine.best_dots();
  const auto count = std::min<std::size_t>(capacity, dots.size());

  for (std::size_t index = 0; index < count; ++index) {
    output[index] = to_c_dot(dots[index]);
  }

  return count;
}

/** Returns the byte length of the current best SVG export. */
size_t stippling_engine_best_svg_byte_length(const StipplingEngine* engine,
                                             int scale) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return 0;
  }

  return engine->engine.export_best_svg(scale).size();
}

/** Copies the current best SVG export into a caller-owned buffer. */
size_t stippling_engine_copy_best_svg(const StipplingEngine* engine,
                                      char* output,
                                      size_t capacity,
                                      int scale) {
  if (engine == nullptr || output == nullptr || !engine->engine.has_optimizer()) {
    return 0;
  }

  const auto svg = engine->engine.export_best_svg(scale);
  const auto count = std::min<std::size_t>(capacity, svg.size());
  std::copy_n(svg.data(), count, output);
  return count;
}

/** Returns the byte length of the current best PNG export. */
size_t stippling_engine_best_png_byte_length(const StipplingEngine* engine,
                                             int scale) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return 0;
  }

  return engine->engine.export_best_png(scale).size();
}

/** Copies the current best PNG export into a caller-owned buffer. */
size_t stippling_engine_copy_best_png(const StipplingEngine* engine,
                                      uint8_t* output,
                                      size_t capacity,
                                      int scale) {
  if (engine == nullptr || output == nullptr || !engine->engine.has_optimizer()) {
    return 0;
  }

  const auto png = engine->engine.export_best_png(scale);
  const auto count = std::min<std::size_t>(capacity, png.size());
  std::copy_n(png.data(), count, output);
  return count;
}

/** Returns the current target statistics as a C ABI struct. */
StipplingTargetStats stippling_engine_target_stats(const StipplingEngine* engine) {
  if (engine == nullptr) {
    return {};
  }

  return to_c_stats(engine->engine.target_stats());
}

/** Returns the current target's black-pixel count. */
uint32_t stippling_engine_target_black_pixels(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->engine.target_stats().black_pixels;
}

/** Returns the current target's total pixel count. */
uint32_t stippling_engine_target_total_pixels(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->engine.target_stats().total_pixels;
}

/** Returns the current target's black-pixel percentage. */
double stippling_engine_target_black_percentage(const StipplingEngine* engine) {
  return engine == nullptr ? 0.0 : engine->engine.target_stats().black_percentage;
}

/** Returns the engine's recommended dot count for the current target. */
uint32_t stippling_engine_target_recommended_dot_count(const StipplingEngine* engine) {
  return engine == nullptr ? 0u
                           : engine->engine.target_stats().recommended_dot_count;
}

/** Returns the latest optimizer progress as a C ABI struct. */
StipplingOptimizerProgress stippling_engine_optimizer_progress(
    const StipplingEngine* engine) {
  if (engine == nullptr) {
    return {};
  }

  return to_c_progress(engine->engine.optimizer_progress());
}

/** Returns the latest whole-run optimizer generation count. */
uint32_t stippling_engine_optimizer_generation(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->engine.optimizer_progress().generation;
}

/** Returns the latest best fitness value. */
double stippling_engine_optimizer_best_fitness(const StipplingEngine* engine) {
  return engine == nullptr ? 0.0 : engine->engine.optimizer_progress().best_fitness;
}

/** Returns the latest best squared-error value. */
uint64_t stippling_engine_optimizer_best_squared_error(
    const StipplingEngine* engine) {
  return engine == nullptr ? 0u
                           : engine->engine.optimizer_progress().best_squared_error;
}

/** Validates the optimizer's incremental raster bookkeeping. */
StipplingOptimizerValidation stippling_engine_validate_optimizer(
    const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return {};
  }

  return to_c_validation(engine->engine.validate_optimizer());
}

/** Returns the last stored error message for a handle. */
const char* stippling_engine_last_error(const StipplingEngine* engine) {
  return engine == nullptr ? "Engine handle is null" : engine->last_error.c_str();
}
