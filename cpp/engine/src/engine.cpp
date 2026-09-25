#include "stippling/engine/engine.hpp"

// Preprocessing, the multiscale pyramid, and export. The search at each level
// lives in optimizer.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
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

constexpr double kRecommendedDotsPerImportancePoint = 0.035;
constexpr double kMaxRecommendedDotPercentage = 0.04;

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

std::uint8_t clamp_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
}

std::vector<double> extract_grayscale_channel(const ImageBuffer& image) {
  const auto pixel_count =
      static_cast<std::size_t>(image.width * image.height);
  std::vector<double> grayscale(pixel_count, 0.0);

  if (image.format == PixelFormat::grayscale8) {
    for (std::size_t index = 0; index < pixel_count; ++index) {
      grayscale[index] = static_cast<double>(image.pixels[index]);
    }
    return grayscale;
  }

  for (std::size_t index = 0; index < pixel_count; ++index) {
    const auto pixel_index = index * 4u;
    grayscale[index] = 0.299 * static_cast<double>(image.pixels[pixel_index]) +
                       0.587 * static_cast<double>(image.pixels[pixel_index + 1u]) +
                       0.114 * static_cast<double>(image.pixels[pixel_index + 2u]);
  }

  return grayscale;
}

/**
 * exp(-a) for a >= 0, using only +, *, and / in a fixed order. libm's exp is
 * not correctly rounded and differs between platforms (glibc vs. Emscripten's
 * musl), which would make native and WASM blur targets differently.
 */
double portable_exp_negative(double a) {
  double term = 1.0;
  double sum = 1.0;
  for (int n = 1; n < 80 && term > sum * 1e-17; ++n) {
    term *= a / static_cast<double>(n);
    sum += term;
  }
  return 1.0 / sum;
}

std::vector<double> build_gaussian_kernel(std::uint32_t blur_amount) {
  if (blur_amount == 0) {
    return {1.0};
  }

  const auto sigma = std::max(0.85, static_cast<double>(blur_amount) * 0.65);
  const auto radius =
      std::max<int>(1, static_cast<int>(std::ceil(sigma * 2.0)));
  std::vector<double> kernel(static_cast<std::size_t>(radius * 2 + 1), 0.0);
  double weight_sum = 0.0;

  for (int offset = -radius; offset <= radius; ++offset) {
    const auto weight =
        portable_exp_negative((offset * offset) / (2.0 * sigma * sigma));
    kernel[static_cast<std::size_t>(offset + radius)] = weight;
    weight_sum += weight;
  }

  for (auto& weight : kernel) {
    weight /= weight_sum;
  }

  return kernel;
}

std::vector<double> apply_separable_blur(const std::vector<double>& source,
                                         int width,
                                         int height,
                                         std::uint32_t blur_amount) {
  if (blur_amount == 0 || source.empty()) {
    return source;
  }

  const auto kernel = build_gaussian_kernel(blur_amount);
  const auto radius = static_cast<int>((kernel.size() - 1) / 2);
  std::vector<double> horizontal(source.size(), 0.0);
  std::vector<double> blurred(source.size(), 0.0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double value = 0.0;
      for (int offset = -radius; offset <= radius; ++offset) {
        const auto sample_x = std::clamp(x + offset, 0, width - 1);
        value += source[static_cast<std::size_t>(y * width + sample_x)] *
                 kernel[static_cast<std::size_t>(offset + radius)];
      }
      horizontal[static_cast<std::size_t>(y * width + x)] = value;
    }
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double value = 0.0;
      for (int offset = -radius; offset <= radius; ++offset) {
        const auto sample_y = std::clamp(y + offset, 0, height - 1);
        value += horizontal[static_cast<std::size_t>(sample_y * width + x)] *
                 kernel[static_cast<std::size_t>(offset + radius)];
      }
      blurred[static_cast<std::size_t>(y * width + x)] = value;
    }
  }

  return blurred;
}

std::vector<double> compute_edge_response(const std::vector<double>& grayscale,
                                          int width,
                                          int height) {
  if (grayscale.empty()) {
    return {};
  }

  std::vector<double> edges(grayscale.size(), 0.0);

  const auto sample = [&](int x, int y) {
    const auto clamped_x = std::clamp(x, 0, width - 1);
    const auto clamped_y = std::clamp(y, 0, height - 1);
    return grayscale[static_cast<std::size_t>(clamped_y * width + clamped_x)];
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto gx = -sample(x - 1, y - 1) + sample(x + 1, y - 1) -
                      2.0 * sample(x - 1, y) + 2.0 * sample(x + 1, y) -
                      sample(x - 1, y + 1) + sample(x + 1, y + 1);
      const auto gy = sample(x - 1, y - 1) + 2.0 * sample(x, y - 1) +
                      sample(x + 1, y - 1) - sample(x - 1, y + 1) -
                      2.0 * sample(x, y + 1) - sample(x + 1, y + 1);
      const auto magnitude = std::sqrt(gx * gx + gy * gy);
      edges[static_cast<std::size_t>(y * width + x)] =
          clamp_unit(magnitude / 1020.0);
    }
  }

  return edges;
}

std::vector<double> compute_local_structure(const std::vector<double>& grayscale,
                                            const std::vector<double>& blurred) {
  std::vector<double> structure(grayscale.size(), 0.0);

  for (std::size_t index = 0; index < grayscale.size(); ++index) {
    structure[index] =
        clamp_unit(std::abs(grayscale[index] - blurred[index]) / 255.0);
  }

  return structure;
}

std::vector<double> combine_importance(const std::vector<double>& grayscale,
                                       const std::vector<double>& edges,
                                       const std::vector<double>& structure) {
  std::vector<double> importance(grayscale.size(), 0.0);

  for (std::size_t index = 0; index < grayscale.size(); ++index) {
    const auto darkness = clamp_unit((255.0 - grayscale[index]) / 255.0);
    const auto edge_weight = index < edges.size() ? edges[index] : 0.0;
    const auto structure_weight =
        index < structure.size() ? structure[index] : 0.0;
    auto combined = 0.55 * darkness + 0.30 * edge_weight +
                    0.15 * structure_weight;

    // A small darkness floor prevents broad shadows from vanishing when edge
    // energy is low, while still letting strong edges dominate allocation.
    if (darkness > 0.1) {
      combined = std::max(combined, darkness * 0.35);
    }

    importance[index] = clamp_unit(combined);
  }

  return importance;
}

std::vector<std::uint8_t> quantize_channel(const std::vector<double>& source) {
  std::vector<std::uint8_t> quantized(source.size(), 255u);
  for (std::size_t index = 0; index < source.size(); ++index) {
    quantized[index] = clamp_byte(source[index]);
  }
  return quantized;
}

std::vector<std::uint8_t> threshold_channel(
    const std::vector<std::uint8_t>& source,
    std::uint32_t threshold) {
  std::vector<std::uint8_t> thresholded(source.size(), 255u);
  for (std::size_t index = 0; index < source.size(); ++index) {
    thresholded[index] =
        source[index] < threshold ? static_cast<std::uint8_t>(0u)
                                  : static_cast<std::uint8_t>(255u);
  }
  return thresholded;
}

ImageBuffer rgba_from_grayscale(const std::vector<std::uint8_t>& grayscale,
                                int width,
                                int height) {
  std::vector<std::uint8_t> rgba(
      static_cast<std::size_t>(width * height * 4), 255u);

  for (std::size_t index = 0; index < grayscale.size(); ++index) {
    const auto pixel_index = index * 4u;
    rgba[pixel_index] = grayscale[index];
    rgba[pixel_index + 1u] = grayscale[index];
    rgba[pixel_index + 2u] = grayscale[index];
  }

  return {
      .format = PixelFormat::rgba8,
      .width = width,
      .height = height,
      .pixels = std::move(rgba),
  };
}

/**
 * Recommends a dot count from total importance, capped by image area, with a
 * floor based on black pixels so sparse silhouettes still get enough dots.
 */
TargetStats calculate_target_stats(const std::vector<std::uint8_t>& thresholded,
                                   const std::vector<double>& importance,
                                   int width,
                                   int height,
                                   std::uint32_t max_dot_count) {
  TargetStats stats{};
  stats.total_pixels = static_cast<std::uint32_t>(width * height);

  for (const auto value : thresholded) {
    if (value == 0) {
      ++stats.black_pixels;
    }
  }

  stats.black_percentage =
      stats.total_pixels == 0
          ? 0.0
          : static_cast<double>(stats.black_pixels) /
                static_cast<double>(stats.total_pixels);

  const auto total_importance = std::accumulate(importance.begin(),
                                                importance.end(), 0.0);
  if (total_importance <= 0.0 && stats.black_pixels == 0) {
    stats.recommended_dot_count = 0;
    return stats;
  }

  const auto importance_based_count = static_cast<std::uint32_t>(
      std::ceil(total_importance * kRecommendedDotsPerImportancePoint));
  const auto area_capped_count = static_cast<std::uint32_t>(
      std::ceil(static_cast<double>(stats.total_pixels) *
                kMaxRecommendedDotPercentage));
  stats.recommended_dot_count =
      std::min(max_dot_count,
               std::max<std::uint32_t>(
                   1u,
                   std::min(area_capped_count,
                            std::max<std::uint32_t>(
                                importance_based_count,
                                static_cast<std::uint32_t>(
                                    std::ceil(stats.black_pixels * 0.006))))));

  return stats;
}

// Area averaging, unlike point sampling, keeps thin structures from vanishing
// at coarse pyramid levels.
std::vector<std::uint8_t> resample_u8_average(
    const std::vector<std::uint8_t>& source,
    int source_width,
    int source_height,
    int target_width,
    int target_height) {
  if (source_width == target_width && source_height == target_height) {
    return source;
  }

  std::vector<std::uint8_t> resized(
      static_cast<std::size_t>(target_width * target_height), 255u);

  for (int y = 0; y < target_height; ++y) {
    const auto source_y0 =
        static_cast<int>(std::floor(static_cast<double>(y) * source_height /
                                    target_height));
    const auto source_y1 =
        std::max(source_y0 + 1,
                 static_cast<int>(std::ceil(static_cast<double>(y + 1) *
                                            source_height / target_height)));
    for (int x = 0; x < target_width; ++x) {
      const auto source_x0 =
          static_cast<int>(std::floor(static_cast<double>(x) * source_width /
                                      target_width));
      const auto source_x1 =
          std::max(source_x0 + 1,
                   static_cast<int>(std::ceil(static_cast<double>(x + 1) *
                                              source_width / target_width)));

      double sum = 0.0;
      std::size_t count = 0;
      for (int sample_y = source_y0; sample_y < source_y1; ++sample_y) {
        for (int sample_x = source_x0; sample_x < source_x1; ++sample_x) {
          sum += source[static_cast<std::size_t>(sample_y * source_width + sample_x)];
          ++count;
        }
      }

      resized[static_cast<std::size_t>(y * target_width + x)] =
          clamp_byte(sum / static_cast<double>(std::max<std::size_t>(1, count)));
    }
  }

  return resized;
}

std::vector<double> resample_double_average(const std::vector<double>& source,
                                            int source_width,
                                            int source_height,
                                            int target_width,
                                            int target_height) {
  if (source_width == target_width && source_height == target_height) {
    return source;
  }

  std::vector<double> resized(
      static_cast<std::size_t>(target_width * target_height), 0.0);

  for (int y = 0; y < target_height; ++y) {
    const auto source_y0 =
        static_cast<int>(std::floor(static_cast<double>(y) * source_height /
                                    target_height));
    const auto source_y1 =
        std::max(source_y0 + 1,
                 static_cast<int>(std::ceil(static_cast<double>(y + 1) *
                                            source_height / target_height)));
    for (int x = 0; x < target_width; ++x) {
      const auto source_x0 =
          static_cast<int>(std::floor(static_cast<double>(x) * source_width /
                                      target_width));
      const auto source_x1 =
          std::max(source_x0 + 1,
                   static_cast<int>(std::ceil(static_cast<double>(x + 1) *
                                              source_width / target_width)));

      double sum = 0.0;
      std::size_t count = 0;
      for (int sample_y = source_y0; sample_y < source_y1; ++sample_y) {
        for (int sample_x = source_x0; sample_x < source_x1; ++sample_x) {
          sum += source[static_cast<std::size_t>(sample_y * source_width + sample_x)];
          ++count;
        }
      }

      resized[static_cast<std::size_t>(y * target_width + x)] =
          sum / static_cast<double>(std::max<std::size_t>(1, count));
    }
  }

  return resized;
}

// Rescales dots between pixel grids, both for promotion and to report coarse
// levels at full size.
std::vector<Dot> scale_dots_between_spaces(const std::vector<Dot>& dots,
                                           int source_width,
                                           int source_height,
                                           int target_width,
                                           int target_height) {
  if (dots.empty()) {
    return {};
  }

  const auto x_scale = static_cast<double>(target_width) /
                       static_cast<double>(std::max(1, source_width));
  const auto y_scale = static_cast<double>(target_height) /
                       static_cast<double>(std::max(1, source_height));
  const auto radius_scale = (x_scale + y_scale) * 0.5;
  std::vector<Dot> scaled;
  scaled.reserve(dots.size());

  for (const auto& dot : dots) {
    scaled.push_back({
        .x = std::clamp(dot.x * x_scale, 0.0,
                        static_cast<double>(std::max(0, target_width - 1))),
        .y = std::clamp(dot.y * y_scale, 0.0,
                        static_cast<double>(std::max(0, target_height - 1))),
        .radius =
            clamp_dot_radius(dot.radius * radius_scale),
    });
  }

  return scaled;
}

}  // namespace

Engine::Engine() = default;

Engine::~Engine() = default;

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
  reset_run_state();
}

void Engine::reset_run_state() {
  optimizer_.reset();
  current_level_index_ = 0;
  total_generations_ = 0;
  timelapse_frames_.clear();
  timelapse_stride_ = 1;
}

// The optimizer works on the blurred grayscale target and importance map; the
// thresholded preview is only for display and dot-count statistics.
ImageBuffer Engine::prepare_target(const ImageBuffer& source_image,
                                   const TargetProcessingConfig& config) {
  if (!source_image.valid()) {
    throw std::invalid_argument("Source image buffer is invalid");
  }
  if (source_image.format != PixelFormat::rgba8 &&
      source_image.format != PixelFormat::grayscale8) {
    throw std::invalid_argument(
        "prepare_target expects grayscale8 or rgba8 input");
  }

  const auto grayscale = extract_grayscale_channel(source_image);
  const auto blurred =
      apply_separable_blur(grayscale, source_image.width, source_image.height,
                           config.blur_amount);
  const auto edges =
      compute_edge_response(blurred, source_image.width, source_image.height);
  const auto structure = compute_local_structure(grayscale, blurred);

  importance_map_ = combine_importance(blurred, edges, structure);
  optimizer_target_ = quantize_channel(blurred);
  const auto thresholded = threshold_channel(optimizer_target_, config.threshold);

  image_ = rgba_from_grayscale(thresholded, source_image.width, source_image.height);
  target_stats_ = calculate_target_stats(thresholded, importance_map_,
                                         source_image.width, source_image.height,
                                         config.max_dot_count);
  pyramid_.clear();
  reset_run_state();

  return image_;
}

void Engine::initialize_optimizer() {
  if (!has_image() || optimizer_target_.empty() || importance_map_.empty()) {
    throw std::logic_error(
        "Cannot initialize optimizer without a prepared target");
  }

  pyramid_.clear();
  reset_run_state();

  const std::vector<double> scales = {0.125, 0.25, 0.5, 1.0};
  int previous_width = 0;
  int previous_height = 0;

  for (const auto scale : scales) {
    const auto width =
        std::max(1, static_cast<int>(std::round(image_.width * scale)));
    const auto height =
        std::max(1, static_cast<int>(std::round(image_.height * scale)));

    if (width == previous_width && height == previous_height) {
      continue;
    }

    pyramid_.push_back({
        .width = width,
        .height = height,
        .target = resample_u8_average(optimizer_target_, image_.width,
                                      image_.height, width, height),
        .importance = resample_double_average(importance_map_, image_.width,
                                             image_.height, width, height),
    });
    previous_width = width;
    previous_height = height;
  }

  if (pyramid_.empty() || pyramid_.back().width != image_.width ||
      pyramid_.back().height != image_.height) {
    pyramid_.push_back({
        .width = image_.width,
        .height = image_.height,
        .target = optimizer_target_,
        .importance = importance_map_,
    });
  }

  initialize_level_optimizer({});
  maybe_capture_timelapse_frame();
}

// Coarse levels get sqrt(area ratio) of the dot budget: enough to lay out the
// broad structure while staying cheap to search.
void Engine::initialize_level_optimizer(const std::vector<Dot>& seed_dots) {
  if (current_level_index_ >= pyramid_.size()) {
    throw std::logic_error("Requested pyramid level is out of bounds");
  }

  const auto& level = pyramid_[current_level_index_];
  const auto full_area =
      static_cast<double>(std::max(1, image_.width * image_.height));
  const auto level_area =
      static_cast<double>(std::max(1, level.width * level.height));
  const auto dot_scale = std::sqrt(level_area / full_area);

  EngineConfig level_config = config_;
  level_config.dot_count = std::max<std::uint32_t>(
      1u, std::min(config_.dot_count,
                   static_cast<std::uint32_t>(std::round(
                       static_cast<double>(config_.dot_count) * dot_scale))));

  if (seed_dots.empty()) {
    optimizer_ = std::make_unique<Optimizer>(
        level.width, level.height, level.target, level.importance, level_config);
  } else {
    optimizer_ = std::make_unique<Optimizer>(
        level.width, level.height, level.target, level.importance, level_config,
        seed_dots);
  }
  optimizer_->initialize();
}

// Promotion replaces the optimizer with a fresh one for the finer level,
// seeded with the current best dots.
void Engine::maybe_promote_level() {
  if (!optimizer_ || current_level_index_ >= pyramid_.size()) {
    return;
  }

  if (current_level_index_ + 1 >= pyramid_.size()) {
    return;
  }

  const auto& current_level = pyramid_[current_level_index_];
  const auto level_progress = optimizer_->progress();
  const auto minimum_generations_at_level =
      static_cast<std::uint32_t>(2u + current_level_index_);
  if (level_progress.generation < minimum_generations_at_level ||
      !optimizer_->ready_to_promote_for_multiscale()) {
    return;
  }

  const auto& next_level = pyramid_[current_level_index_ + 1];
  auto seed_dots = scale_dots_between_spaces(
      optimizer_->best_dots(), current_level.width, current_level.height,
      next_level.width, next_level.height);
  ++current_level_index_;
  initialize_level_optimizer(seed_dots);
}

OptimizerProgress Engine::evolve_batch() {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  (void)optimizer_->evolve_batch();
  total_generations_ += config_.generations_per_batch;
  maybe_promote_level();
  maybe_capture_timelapse_frame();

  auto progress = optimizer_->progress();
  progress.generation = total_generations_;
  return progress;
}

std::vector<Dot> Engine::best_dots() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  const auto& current_level = pyramid_[current_level_index_];
  return project_dots_to_image_space(optimizer_->best_dots(), current_level.width,
                                     current_level.height);
}

// Over budget, drop every other frame and double the stride: memory stays
// bounded however long the run, and frames stay evenly spaced.
void Engine::maybe_capture_timelapse_frame() {
  if (config_.timelapse_max_frames == 0 ||
      total_generations_ % timelapse_stride_ != 0) {
    return;
  }

  timelapse_frames_.push_back({.generation = total_generations_, .dots = best_dots()});
  if (timelapse_frames_.size() <= config_.timelapse_max_frames) {
    return;
  }

  timelapse_stride_ *= 2u;
  std::erase_if(timelapse_frames_, [&](const TimelapseFrame& frame) {
    return frame.generation % timelapse_stride_ != 0;
  });
}

const std::vector<TimelapseFrame>& Engine::timelapse_frames() const noexcept {
  return timelapse_frames_;
}

OptimizerProgress Engine::optimizer_progress() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  auto progress = optimizer_->progress();
  progress.generation = total_generations_;
  return progress;
}

OptimizerValidation Engine::validate_optimizer() const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return optimizer_->validate_incremental_state();
}

std::vector<Dot> Engine::project_dots_to_image_space(
    const std::vector<Dot>& dots,
    int source_width,
    int source_height) const {
  return scale_dots_between_spaces(dots, source_width, source_height,
                                   image_.width, image_.height);
}

std::string Engine::export_best_svg(int scale) const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  return export_dots_to_svg(best_dots(), image_.width, image_.height, scale);
}

std::string Engine::export_timelapse_svg(int scale,
                                         std::uint32_t frame_duration_ms) const {
  if (!optimizer_) {
    throw std::logic_error("Optimizer has not been initialized");
  }

  auto frames = timelapse_frames_;
  if (frames.empty() || frames.back().generation != total_generations_) {
    frames.push_back({.generation = total_generations_, .dots = best_dots()});
  }
  return export_timelapse_to_svg(frames, image_.width, image_.height, scale,
                                 frame_duration_ms);
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

  return compute_quality_metrics(optimizer_target_, render_best_grayscale());
}

}  // namespace stippling
