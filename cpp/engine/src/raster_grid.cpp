#include "stippling/engine/raster_grid.hpp"

// Invariant: pixels_[i] is black exactly when coverage_[i] > 0.

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stippling {

namespace {

std::uint64_t pixel_squared_error(std::uint8_t pixel, std::uint8_t target) {
  const auto diff = static_cast<int>(pixel) - static_cast<int>(target);
  return static_cast<std::uint64_t>(diff * diff);
}

}  // namespace

RasterGrid::RasterGrid(int width, int height)
    : width_(width),
      height_(height),
      pixels_(static_cast<std::size_t>(width * height), 255),
      coverage_(static_cast<std::size_t>(width * height), 0) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("RasterGrid dimensions must be positive");
  }
}

void RasterGrid::clear() {
  std::fill(pixels_.begin(), pixels_.end(), 255);
  std::fill(coverage_.begin(), coverage_.end(), 0);
}

void RasterGrid::draw_dot(const Dot& dot) {
  rasterize_dot(dot, 1, nullptr, nullptr);
}

void RasterGrid::erase_dot(const Dot& dot) {
  rasterize_dot(dot, -1, nullptr, nullptr);
}

// Touches only the two dots' footprints, not the whole image.
std::uint64_t RasterGrid::apply_dot_delta_and_update_error(
    const Dot& previous_dot,
    const Dot& next_dot,
    const std::vector<std::uint8_t>& target,
    std::uint64_t current_squared_error) {
  if (target.size() != pixels_.size()) {
    throw std::invalid_argument("Target size does not match raster dimensions");
  }

  rasterize_dot(previous_dot, -1, &target, &current_squared_error);
  rasterize_dot(next_dot, 1, &target, &current_squared_error);
  return current_squared_error;
}

std::uint64_t RasterGrid::squared_error(
    const std::vector<std::uint8_t>& target) const {
  if (target.size() != pixels_.size()) {
    throw std::invalid_argument("Target size does not match raster dimensions");
  }

  std::uint64_t diff = 0;
  for (std::size_t index = 0; index < pixels_.size(); ++index) {
    const auto pixel_diff =
        static_cast<int>(pixels_[index]) - static_cast<int>(target[index]);
    diff += static_cast<std::uint64_t>(pixel_diff * pixel_diff);
  }

  return diff;
}

const std::vector<std::uint8_t>& RasterGrid::pixels() const noexcept {
  return pixels_;
}

int RasterGrid::width() const noexcept {
  return width_;
}

int RasterGrid::height() const noexcept {
  return height_;
}

/**
 * Covers each pixel whose center lies inside the dot's circle (the same circle
 * SVG/PNG export draws), plus the pixel containing the center so sub-pixel dots
 * never vanish. `delta` is +1 to draw, -1 to erase.
 */
void RasterGrid::rasterize_dot(const Dot& dot,
                               int delta,
                               const std::vector<std::uint8_t>* target,
                               std::uint64_t* squared_error) {
  if (dot.radius < 0.0) {
    throw std::invalid_argument("Dot radius cannot be negative");
  }

  const auto radius_squared = dot.radius * dot.radius;
  const auto center_column = static_cast<int>(std::floor(dot.x));
  const auto center_row = static_cast<int>(std::floor(dot.y));
  const auto first_row = static_cast<int>(std::ceil(dot.y - dot.radius - 0.5));
  const auto last_row = static_cast<int>(std::floor(dot.y + dot.radius - 0.5));

  for (int row = std::min(first_row, center_row);
       row <= std::max(last_row, center_row); ++row) {
    const auto dy = static_cast<double>(row) + 0.5 - dot.y;
    auto start_x = 0;
    auto end_x = -1;
    if (dy * dy <= radius_squared) {
      const auto half_width = std::sqrt(radius_squared - dy * dy);
      start_x = static_cast<int>(std::ceil(dot.x - half_width - 0.5));
      end_x = static_cast<int>(std::floor(dot.x + half_width - 0.5));
    }
    if (row == center_row) {
      // On the center row any sampled span already touches the center pixel,
      // so widening to include it keeps the span contiguous.
      start_x = start_x <= end_x ? std::min(start_x, center_column) : center_column;
      end_x = std::max(end_x, center_column);
    }
    if (start_x <= end_x) {
      update_horizontal_span(row, start_x, end_x, delta, target, squared_error);
    }
  }
}

void RasterGrid::update_horizontal_span(int y,
                                        int start_x,
                                        int end_x,
                                        int delta,
                                        const std::vector<std::uint8_t>* target,
                                        std::uint64_t* squared_error) {
  if (y < 0 || y >= height_) {
    return;
  }

  start_x = std::max(0, start_x);
  end_x = std::min(width_ - 1, end_x);

  // Accumulate in a local: byte stores into pixels_ may alias *squared_error,
  // which would otherwise force a reload and store on every pixel.
  const auto track_error = target != nullptr && squared_error != nullptr;
  auto error = track_error ? *squared_error : 0u;

  for (int x = start_x; x <= end_x; ++x) {
    const auto index = static_cast<std::size_t>(y * width_ + x);
    const auto previous_pixel = pixels_[index];
    const auto next_count = static_cast<int>(coverage_[index]) + delta;

    if (next_count < 0) {
      throw std::logic_error("Coverage count cannot become negative");
    }

    const auto next_pixel = static_cast<std::uint8_t>(next_count > 0 ? 0 : 255);
    if (track_error) {
      // Branch-free: when the pixel does not change the two terms cancel.
      // Unsigned arithmetic is modular, so the running total stays exact.
      error += pixel_squared_error(next_pixel, (*target)[index]) -
               pixel_squared_error(previous_pixel, (*target)[index]);
    }

    coverage_[index] = static_cast<std::uint16_t>(next_count);
    pixels_[index] = next_pixel;
  }

  if (track_error) {
    *squared_error = error;
  }
}

}  // namespace stippling
