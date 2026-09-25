#include "stippling/engine/c_api.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "stippling/engine/engine.hpp"

static_assert(sizeof(StipplingDot) == 3 * sizeof(double));

struct StipplingEngine {
  stippling::Engine engine;
  std::string last_error{};
  std::vector<StipplingDot> best_dots{};
  std::vector<std::uint8_t> export_buffer{};
};

namespace {

// Exceptions must never cross the C ABI: turn them into -1 plus a message
// stored on the handle.
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

void assign_text(std::vector<std::uint8_t>* output, const std::string& text) {
  output->resize(text.size());
  std::memcpy(output->data(), text.data(), text.size());
}

}  // namespace

StipplingEngine* stippling_engine_create(void) {
  return new StipplingEngine{};
}

void stippling_engine_destroy(StipplingEngine* engine) {
  delete engine;
}

int stippling_engine_configure(StipplingEngine* engine,
                               uint32_t population_size,
                               double mutation_rate,
                               uint32_t dot_count,
                               double elitism_ratio,
                               uint32_t seed,
                               uint32_t generations_per_batch,
                               uint32_t thread_count) {
  return with_error_boundary(engine, [&]() {
    engine->engine.configure({
        .population_size = population_size,
        .mutation_rate = mutation_rate,
        .dot_count = dot_count,
        .elitism_ratio = elitism_ratio,
        .seed = seed,
        .generations_per_batch = generations_per_batch,
        .thread_count = thread_count,
    });
  });
}

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

  return with_error_boundary(engine, [&]() {
    const stippling::ImageBuffer image{
        .format = stippling::PixelFormat::rgba8,
        .width = width,
        .height = height,
        .pixels = std::vector<std::uint8_t>(pixels, pixels + length),
    };
    if (!image.valid()) {
      throw std::invalid_argument("Image byte length does not match its dimensions");
    }

    (void)engine->engine.prepare_target(image, {
                                                   .blur_amount = blur_amount,
                                                   .threshold = threshold,
                                                   .max_dot_count = max_dot_count,
                                               });
  });
}

int stippling_engine_initialize_optimizer(StipplingEngine* engine) {
  return with_error_boundary(engine, [&]() {
    engine->engine.initialize_optimizer();
  });
}

int stippling_engine_evolve_batch(StipplingEngine* engine) {
  return with_error_boundary(engine, [&]() {
    (void)engine->engine.evolve_batch();
  });
}

int stippling_engine_prepared_image_width(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  return engine->engine.image().width;
}

int stippling_engine_prepared_image_height(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  return engine->engine.image().height;
}

size_t stippling_engine_prepared_image_byte_length(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_image()) {
    return 0;
  }

  return engine->engine.image().pixels.size();
}

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

uint32_t stippling_engine_target_black_pixels(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->engine.target_stats().black_pixels;
}

uint32_t stippling_engine_target_total_pixels(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->engine.target_stats().total_pixels;
}

double stippling_engine_target_black_percentage(const StipplingEngine* engine) {
  return engine == nullptr ? 0.0 : engine->engine.target_stats().black_percentage;
}

uint32_t stippling_engine_target_recommended_dot_count(const StipplingEngine* engine) {
  return engine == nullptr ? 0u
                           : engine->engine.target_stats().recommended_dot_count;
}

int stippling_engine_capture_best_dots(StipplingEngine* engine) {
  return with_error_boundary(engine, [&]() {
    const auto dots = engine->engine.best_dots();
    engine->best_dots.resize(dots.size());
    for (std::size_t index = 0; index < dots.size(); ++index) {
      engine->best_dots[index] = {
          .x = dots[index].x,
          .y = dots[index].y,
          .radius = dots[index].radius,
      };
    }
  });
}

const StipplingDot* stippling_engine_best_dots_data(const StipplingEngine* engine) {
  return engine == nullptr ? nullptr : engine->best_dots.data();
}

size_t stippling_engine_best_dots_count(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->best_dots.size();
}

int stippling_engine_export(StipplingEngine* engine,
                            StipplingExportFormat format,
                            int scale,
                            uint32_t frame_duration_ms) {
  return with_error_boundary(engine, [&]() {
    switch (format) {
      case STIPPLING_EXPORT_SVG:
        assign_text(&engine->export_buffer, engine->engine.export_best_svg(scale));
        return;
      case STIPPLING_EXPORT_PNG:
        engine->export_buffer = engine->engine.export_best_png(scale);
        return;
      case STIPPLING_EXPORT_TIMELAPSE_SVG:
        assign_text(&engine->export_buffer,
                    engine->engine.export_timelapse_svg(scale, frame_duration_ms));
        return;
    }
    throw std::invalid_argument("Unknown export format");
  });
}

const uint8_t* stippling_engine_export_data(const StipplingEngine* engine) {
  return engine == nullptr ? nullptr : engine->export_buffer.data();
}

size_t stippling_engine_export_size(const StipplingEngine* engine) {
  return engine == nullptr ? 0u : engine->export_buffer.size();
}

uint32_t stippling_engine_optimizer_generation(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return 0u;
  }

  return engine->engine.optimizer_progress().generation;
}

double stippling_engine_optimizer_best_fitness(const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return 0.0;
  }

  return engine->engine.optimizer_progress().best_fitness;
}

uint64_t stippling_engine_optimizer_best_squared_error(
    const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return 0u;
  }

  return engine->engine.optimizer_progress().best_squared_error;
}

StipplingOptimizerValidation stippling_engine_validate_optimizer(
    const StipplingEngine* engine) {
  if (engine == nullptr || !engine->engine.has_optimizer()) {
    return {};
  }

  const auto validation = engine->engine.validate_optimizer();
  return {
      .valid = validation.valid ? 1 : 0,
      .checked_candidates = validation.checked_candidates,
      .mismatched_candidates = validation.mismatched_candidates,
      .first_mismatch_index = validation.first_mismatch_index,
      .max_squared_error_delta = validation.max_squared_error_delta,
      .total_pixel_mismatches = validation.total_pixel_mismatches,
  };
}

const char* stippling_engine_last_error(const StipplingEngine* engine) {
  return engine == nullptr ? "Engine handle is null" : engine->last_error.c_str();
}
