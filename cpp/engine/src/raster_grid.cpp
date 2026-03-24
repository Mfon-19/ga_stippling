#include "stippling/engine/raster_grid.hpp"

// raster_grid.cpp implements the low-level binary raster used by each optimizer
// candidate.
//
// At a high level, this file is responsible for:
// - storing the rendered stipple image as a binary black/white pixel buffer
// - tracking per-pixel coverage counts so overlapping dots can be added and
//   removed safely
// - rasterizing filled circular dots into that binary image
// - updating squared error incrementally when a dot is replaced
// - providing a slower full-image squared-error path for validation
//
// The key invariant is that `pixels_` and `coverage_` must stay in sync. The
// optimizer relies on this file to answer "what happens to the image error if
// I remove this dot and add that one?" without redrawing the entire candidate.

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stippling {

namespace {

/** Returns one pixel's squared contribution to the image error. */
std::uint64_t pixel_squared_error(std::uint8_t pixel, std::uint8_t target) {
  const auto diff = static_cast<int>(pixel) - static_cast<int>(target);
  return static_cast<std::uint64_t>(diff * diff);
}

}  // namespace

/** Allocates a blank binary raster and its matching coverage-count buffer. */
RasterGrid::RasterGrid(int width, int height)
    : width_(width),
      height_(height),
      pixels_(static_cast<std::size_t>(width * height), 255),
      coverage_(static_cast<std::size_t>(width * height), 0) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("RasterGrid dimensions must be positive");
  }
}

/** Resets the raster back to an empty white image with zero coverage. */
void RasterGrid::clear() {
  std::fill(pixels_.begin(), pixels_.end(), 255);
  std::fill(coverage_.begin(), coverage_.end(), 0);
}

/** Draws one filled dot into the raster. */
void RasterGrid::draw_dot(const Dot& dot) {
  rasterize_dot(dot, 1, nullptr, nullptr);
}

/** Erases one filled dot from the raster. */
void RasterGrid::erase_dot(const Dot& dot) {
  rasterize_dot(dot, -1, nullptr, nullptr);
}

/** Replaces one dot with another without updating squared-error bookkeeping. */
void RasterGrid::apply_dot_delta(const Dot& previous_dot, const Dot& next_dot) {
  erase_dot(previous_dot);
  draw_dot(next_dot);
}

/**
 * Replaces one dot with another while updating the current squared error only
 * over the pixels touched by those two dot footprints.
 */
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

/** Recomputes full-image squared error against the supplied target raster. */
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

/** Returns the rendered binary pixel buffer. */
const std::vector<std::uint8_t>& RasterGrid::pixels() const noexcept {
  return pixels_;
}

/** Returns the raster width in pixels. */
int RasterGrid::width() const noexcept {
  return width_;
}

/** Returns the raster height in pixels. */
int RasterGrid::height() const noexcept {
  return height_;
}

/**
 * Converts a dot into integer circle parameters and dispatches to the filled
 * circle rasterizer in either draw (`delta = +1`) or erase (`delta = -1`) mode.
 */
void RasterGrid::rasterize_dot(const Dot& dot,
                               int delta,
                               const std::vector<std::uint8_t>* target,
                               std::uint64_t* squared_error) {
  const auto center_x = static_cast<int>(std::floor(dot.x));
  const auto center_y = static_cast<int>(std::floor(dot.y));
  const auto radius = static_cast<int>(std::floor(dot.radius));

  if (radius < 0) {
    throw std::invalid_argument("Dot radius cannot be negative");
  }

  draw_circle(center_x, center_y, radius, delta, target, squared_error);
}

/**
 * Rasterizes a filled circle using midpoint-circle stepping and horizontal
 * spans. This keeps the dot footprint symmetric while letting span updates
 * reuse one shared coverage/error path.
 */
void RasterGrid::draw_circle(int center_x,
                             int center_y,
                             int radius,
                             int delta,
                             const std::vector<std::uint8_t>* target,
                             std::uint64_t* squared_error) {
  int x = 0;
  int y = radius;
  int decision = 1 - radius;

  update_horizontal_span(center_y, center_x - radius, center_x + radius, delta,
                         target, squared_error);

  while (y > x) {
    if (decision < 0) {
      decision += 2 * x + 3;
    } else {
      decision += 2 * (x - y) + 5;
      --y;
    }
    ++x;

    update_horizontal_span(center_y + y, center_x - x, center_x + x, delta,
                           target, squared_error);
    update_horizontal_span(center_y - y, center_x - x, center_x + x, delta,
                           target, squared_error);
    update_horizontal_span(center_y + x, center_x - y, center_x + y, delta,
                           target, squared_error);
    update_horizontal_span(center_y - x, center_x - y, center_x + y, delta,
                           target, squared_error);
  }
}

/**
 * Applies one horizontal span to the raster. Coverage counts decide whether a
 * pixel should remain black after overlaps, and optional error bookkeeping only
 * updates squared error when the pixel's rendered value actually changes.
 */
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

  for (int x = start_x; x <= end_x; ++x) {
    const auto index = static_cast<std::size_t>(y * width_ + x);
    const auto previous_pixel = pixels_[index];
    const auto next_count = static_cast<int>(coverage_[index]) + delta;

    if (next_count < 0) {
      throw std::logic_error("Coverage count cannot become negative");
    }

    const auto next_pixel = static_cast<std::uint8_t>(next_count > 0 ? 0 : 255);
    if (target != nullptr && squared_error != nullptr && previous_pixel != next_pixel) {
      *squared_error -= pixel_squared_error(previous_pixel, (*target)[index]);
      *squared_error += pixel_squared_error(next_pixel, (*target)[index]);
    }

    coverage_[index] = static_cast<std::uint16_t>(next_count);
    pixels_[index] = next_pixel;
  }
}

}  // namespace stippling
