#include "stippling/engine/engine.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include "stippling/engine/export.hpp"
#include "stippling/engine/optimizer.hpp"

namespace stippling {

bool ImageBuffer::valid() const noexcept {
  if (width <= 0 || height <= 0) {
    return false;
  }

  const auto channels = format == PixelFormat::rgba8 ? 4u : 1u;
  const auto expected_size = static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height) * channels;

  return pixels.size() == expected_size;
}

namespace {

constexpr double kRecommendedDotsPerBlackPixel = 0.02;
constexpr double kMaxRecommendedDotPercentage = 0.04;

void convert_to_grayscale(ImageBuffer& image) {
  for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
    const auto gray = static_cast<std::uint8_t>(
        0.299 * image.pixels[index] + 0.587 * image.pixels[index + 1] +
        0.114 * image.pixels[index + 2]);
    image.pixels[index] = gray;
    image.pixels[index + 1] = gray;
    image.pixels[index + 2] = gray;
  }
}

std::vector<std::uint8_t> blur_rgba(const ImageBuffer& image,
                                    std::uint32_t blur_amount) {
  std::vector<std::uint8_t> blurred(image.pixels.size(), 0);

  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      std::uint32_t sum = 0;
      std::uint32_t count = 0;

      for (int dy = -static_cast<int>(blur_amount);
           dy <= static_cast<int>(blur_amount); ++dy) {
        for (int dx = -static_cast<int>(blur_amount);
             dx <= static_cast<int>(blur_amount); ++dx) {
          const auto next_x = x + dx;
          const auto next_y = y + dy;

          if (next_x < 0 || next_x >= image.width || next_y < 0 ||
              next_y >= image.height) {
            continue;
          }

          const auto index =
              static_cast<std::size_t>((next_y * image.width + next_x) * 4);
          sum += image.pixels[index];
          ++count;
        }
      }

      const auto blurred_value =
          static_cast<std::uint8_t>(std::round(sum / static_cast<double>(count)));
      const auto index = static_cast<std::size_t>((y * image.width + x) * 4);

      blurred[index] = blurred_value;
      blurred[index + 1] = blurred_value;
      blurred[index + 2] = blurred_value;
      blurred[index + 3] = image.pixels[index + 3];
    }
  }

  return blurred;
}

void apply_threshold(ImageBuffer& image, std::uint32_t threshold) {
  for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
    const auto value =
        image.pixels[index] < threshold ? static_cast<std::uint8_t>(0)
                                        : static_cast<std::uint8_t>(255);
    image.pixels[index] = value;
    image.pixels[index + 1] = value;
    image.pixels[index + 2] = value;
  }
}

TargetStats calculate_target_stats(const ImageBuffer& image,
                                   std::uint32_t max_dot_count) {
  TargetStats stats{};
  stats.total_pixels = static_cast<std::uint32_t>(image.width * image.height);

  for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
    if (image.pixels[index] == 0) {
      ++stats.black_pixels;
    }
  }

  stats.black_percentage =
      stats.total_pixels == 0
          ? 0.0
          : static_cast<double>(stats.black_pixels) /
                static_cast<double>(stats.total_pixels);

  if (stats.black_pixels == 0) {
    stats.recommended_dot_count = 0;
    return stats;
  }

  // Recommend one dot for roughly every 50 dark pixels, then cap the result
  // by image area so dark photos do not immediately saturate the canvas.
  const auto density_based_count = static_cast<std::uint32_t>(
      std::ceil(static_cast<double>(stats.black_pixels) *
                kRecommendedDotsPerBlackPixel));
  const auto area_capped_count = static_cast<std::uint32_t>(
      std::ceil(static_cast<double>(stats.total_pixels) *
                kMaxRecommendedDotPercentage));
  stats.recommended_dot_count =
      std::min(max_dot_count,
               std::max<std::uint32_t>(
                   1u, std::min(density_based_count, area_capped_count)));

  return stats;
}

std::vector<std::uint8_t> extract_target_channel(const ImageBuffer& image) {
  std::vector<std::uint8_t> target(
      static_cast<std::size_t>(image.width * image.height), 255);

  if (image.format == PixelFormat::grayscale8) {
    return image.pixels;
  }

  for (std::size_t target_index = 0; target_index < target.size(); ++target_index) {
    target[target_index] = image.pixels[target_index * 4];
  }

  return target;
}

}  // namespace

Engine::Engine() {
  capabilities_.incremental_fitness = true;
  capabilities_.benchmark_mode = true;
  capabilities_.export_svg = true;
  capabilities_.export_png = true;
  status_ = EngineStatus::idle;
}

Engine::~Engine() = default;

const EngineCapabilities& Engine::capabilities() const noexcept {
  return capabilities_;
}

EngineStatus Engine::status() const noexcept {
  return status_;
}

const EngineConfig& Engine::config() const noexcept {
  return config_;
}

const ImageBuffer& Engine::image() const noexcept {
  return image_;
}

const TargetStats& Engine::target_stats() const noexcept {
  return target_stats_;
}

bool Engine::has_image() const noexcept {
  return image_.valid();
}

bool Engine::has_optimizer() const noexcept {
  return optimizer_ != nullptr;
}

void Engine::configure(const EngineConfig& config) {
  config_ = config;
  optimizer_.reset();
  status_ = has_image() ? EngineStatus::image_loaded : EngineStatus::configured;
}

void Engine::load_image(ImageBuffer image) {
  if (!image.valid()) {
    throw std::invalid_argument("ImageBuffer size does not match its format");
  }

  image_ = std::move(image);
  optimizer_.reset();
  status_ = EngineStatus::image_loaded;
}

ImageBuffer Engine::prepare_target(const ImageBuffer& source_image,
                                   const TargetProcessingConfig& config) {
  if (!source_image.valid()) {
    throw std::invalid_argument("Source image buffer is invalid");
  }
  if (source_image.format != PixelFormat::rgba8 &&
      source_image.format != PixelFormat::grayscale8) {
    throw std::invalid_argument("prepare_target expects grayscale8 or rgba8 input");
  }

  ImageBuffer processed = source_image;
  if (processed.format == PixelFormat::grayscale8) {
    std::vector<std::uint8_t> rgba_pixels(
        static_cast<std::size_t>(processed.width * processed.height * 4), 255);
    for (std::size_t index = 0; index < processed.pixels.size(); ++index) {
      rgba_pixels[index * 4] = processed.pixels[index];
      rgba_pixels[index * 4 + 1] = processed.pixels[index];
      rgba_pixels[index * 4 + 2] = processed.pixels[index];
    }
    processed.format = PixelFormat::rgba8;
    processed.pixels = std::move(rgba_pixels);
  }

  convert_to_grayscale(processed);

  if (config.blur_amount > 0) {
    processed.pixels = blur_rgba(processed, config.blur_amount);
  }

  apply_threshold(processed, config.threshold);
  image_ = processed;
  target_stats_ = calculate_target_stats(processed, config.max_dot_count);
  optimizer_.reset();
  status_ = EngineStatus::image_loaded;

  return processed;
}

void Engine::initialize_optimizer() {
  if (!has_image()) {
    throw std::logic_error("Cannot initialize optimizer without a prepared target");
  }

  optimizer_ = std::make_unique<Optimizer>(
      image_.width, image_.height, extract_target_channel(image_), config_);
  optimizer_->initialize();
}

OptimizerProgress Engine::evolve_batch() {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return optimizer_->evolve_batch();
}

const std::vector<Dot>& Engine::best_dots() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return optimizer_->best_dots();
}

OptimizerProgress Engine::optimizer_progress() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return optimizer_->progress();
}

OptimizerValidation Engine::validate_optimizer() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return optimizer_->validate_incremental_state();
}

std::string Engine::export_best_svg(int scale) const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return export_dots_to_svg(best_dots(), image_.width, image_.height, scale);
}

std::vector<std::uint8_t> Engine::export_best_png(int scale) const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return export_dots_to_png(best_dots(), image_.width, image_.height, scale);
}

std::vector<std::uint8_t> Engine::render_best_grayscale(int scale) const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return render_dots_to_grayscale(best_dots(), image_.width, image_.height, scale);
}

QualityMetrics Engine::best_quality_metrics() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }
  if (image_.format != PixelFormat::rgba8) {
    throw std::logic_error("Quality metrics require an rgba8 prepared target");
  }

  return compute_quality_metrics(extract_target_channel(image_), render_best_grayscale());
}

std::string Engine::status_string() const {
  return to_string(status_);
}

std::string to_string(EngineStatus status) {
  switch (status) {
    case EngineStatus::booting:
      return "booting";
    case EngineStatus::idle:
      return "idle";
    case EngineStatus::configured:
      return "configured";
    case EngineStatus::image_loaded:
      return "image_loaded";
  }

  throw std::invalid_argument("Unknown EngineStatus");
}

}  // namespace stippling
