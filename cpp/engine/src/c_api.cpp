#include "stippling/engine/c_api.h"

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

stippling::PixelFormat to_cpp_format(StipplingPixelFormat format) {
  switch (format) {
    case STIPPLING_PIXEL_FORMAT_GRAYSCALE8:
      return stippling::PixelFormat::grayscale8;
    case STIPPLING_PIXEL_FORMAT_RGBA8:
      return stippling::PixelFormat::rgba8;
  }

  throw std::invalid_argument("Unknown pixel format");
}

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

stippling::TargetProcessingConfig to_cpp_processing_config(
    const StipplingTargetProcessingConfig& config) {
  return {
      .blur_amount = config.blur_amount,
      .threshold = config.threshold,
      .max_dot_count = config.max_dot_count,
  };
}

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

StipplingTargetStats to_c_stats(const stippling::TargetStats& stats) {
  return {
      .black_pixels = stats.black_pixels,
      .total_pixels = stats.total_pixels,
      .black_percentage = stats.black_percentage,
      .recommended_dot_count = stats.recommended_dot_count,
  };
}

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

StipplingEngine* stippling_engine_create(void) {
  return new StipplingEngine{};
}

void stippling_engine_destroy(StipplingEngine* engine) {
  delete engine;
}

int stippling_engine_configure(StipplingEngine* engine,
                               const StipplingEngineConfig* config) {
  if (config == nullptr) {
    return -1;
  }

  return with_error_boundary(engine, [&]() {
    engine->engine.configure(to_cpp_config(*config));
  });
}

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

StipplingTargetStats stippling_engine_target_stats(const StipplingEngine* engine) {
  if (engine == nullptr) {
    return {};
  }

  return to_c_stats(engine->engine.target_stats());
}

const char* stippling_engine_last_error(const StipplingEngine* engine) {
  return engine == nullptr ? "Engine handle is null" : engine->last_error.c_str();
}
