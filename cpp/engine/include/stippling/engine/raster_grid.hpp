#pragma once

#include <cstdint>
#include <vector>

#include "stippling/engine/dot.hpp"

namespace stippling {

/**
 * Per-pixel coverage counts; a pixel is black exactly when its count is > 0, so
 * erasing one dot leaves pixels that other dots still cover black. Only counts
 * are stored, which keeps candidate copies small. Tracks squared error
 * incrementally.
 */
class RasterGrid {
 public:
  RasterGrid(int width, int height);

  void clear();
  void draw_dot(const Dot& dot);
  void erase_dot(const Dot& dot);
  [[nodiscard]] std::uint64_t apply_dot_delta_and_update_error(
      const Dot& previous_dot,
      const Dot& next_dot,
      const std::vector<std::uint8_t>& target,
      std::uint64_t current_squared_error);

  [[nodiscard]] std::uint64_t squared_error(
      const std::vector<std::uint8_t>& target) const;
  /** Rendered image (0 = black, 255 = white), built on demand. */
  [[nodiscard]] std::vector<std::uint8_t> pixels() const;
  [[nodiscard]] const std::vector<std::uint16_t>& coverage() const noexcept;
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

 private:
  int width_;
  int height_;
  std::vector<std::uint16_t> coverage_;

  void rasterize_dot(const Dot& dot,
                     int delta,
                     const std::vector<std::uint8_t>* target,
                     std::uint64_t* squared_error);
  void update_horizontal_span(int y,
                              int start_x,
                              int end_x,
                              int delta,
                              const std::vector<std::uint8_t>* target,
                              std::uint64_t* squared_error);
};

}  // namespace stippling
