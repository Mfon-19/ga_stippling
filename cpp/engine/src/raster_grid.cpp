#include "stippling/engine/raster_grid.hpp"

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

void RasterGrid::apply_dot_delta(const Dot& previous_dot, const Dot& next_dot) {
  erase_dot(previous_dot);
  draw_dot(next_dot);
}

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
