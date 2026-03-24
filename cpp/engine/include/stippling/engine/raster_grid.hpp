#pragma once

#include <cstdint>
#include <vector>

#include "stippling/engine/dot.hpp"

namespace stippling {

/**
 * Binary raster plus overlap counts used to validate incremental updates.
 * Coverage counts allow a dot to be removed without incorrectly erasing pixels
 * that are still covered by other dots.
 */
class RasterGrid {
 public:
  RasterGrid(int width, int height);

  void clear();
  void draw_dot(const Dot& dot);
  void erase_dot(const Dot& dot);
  void apply_dot_delta(const Dot& previous_dot, const Dot& next_dot);

  [[nodiscard]] std::uint64_t squared_error(
      const std::vector<std::uint8_t>& target) const;
  [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept;
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

 private:
  int width_;
  int height_;
  std::vector<std::uint8_t> pixels_;
  std::vector<std::uint16_t> coverage_;

  void rasterize_dot(const Dot& dot, int delta);
  void draw_circle(int center_x, int center_y, int radius, int delta);
  void update_horizontal_span(int y, int start_x, int end_x, int delta);
};

}  // namespace stippling
