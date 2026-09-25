#include "stippling/engine/dot.hpp"
#include "stippling/engine/raster_grid.hpp"

#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {

/**
 * Reference footprint: a pixel is covered when its center lies inside the
 * circle, and a dot always covers the pixel containing its own center.
 */
std::vector<std::uint8_t> brute_force_render(const std::vector<stippling::Dot>& dots,
                                             int width,
                                             int height) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height), 255);
  for (const auto& dot : dots) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto dx = x + 0.5 - dot.x;
        const auto dy = y + 0.5 - dot.y;
        const auto contains_center =
            x == static_cast<int>(std::floor(dot.x)) &&
            y == static_cast<int>(std::floor(dot.y));
        if (dx * dx + dy * dy <= dot.radius * dot.radius || contains_center) {
          pixels[static_cast<std::size_t>(y * width + x)] = 0;
        }
      }
    }
  }
  return pixels;
}

}  // namespace

int main() {
  // Replacing a dot incrementally matches drawing the new dot from scratch.
  {
    stippling::RasterGrid incremental(32, 32);
    stippling::RasterGrid full(32, 32);
    const std::vector<std::uint8_t> target(32 * 32, 255);
    const stippling::Dot previous{10.0, 10.0, 3.0};
    const stippling::Dot next{18.0, 12.0, 3.0};

    incremental.draw_dot(previous);
    (void)incremental.apply_dot_delta_and_update_error(
        previous, next, target, incremental.squared_error(target));
    full.draw_dot(next);

    CHECK(incremental.pixels() == full.pixels());
  }

  // Erasing one of two overlapping dots keeps the shared pixels covered.
  {
    stippling::RasterGrid overlapping(32, 32);
    stippling::RasterGrid full(32, 32);
    const stippling::Dot left{10.0, 10.0, 4.0};
    const stippling::Dot right{13.0, 10.0, 4.0};

    overlapping.draw_dot(left);
    overlapping.draw_dot(right);
    overlapping.erase_dot(left);
    full.draw_dot(right);

    CHECK(overlapping.pixels() == full.pixels());
  }

  // A sub-pixel dot covers exactly the pixel containing its center.
  {
    stippling::RasterGrid grid(4, 4);
    grid.draw_dot({.x = 1.95, .y = 2.05, .radius = stippling::kMinDotRadius});
    std::vector<std::uint8_t> expected(16, 255);
    expected[2 * 4 + 1] = 0;
    CHECK(grid.pixels() == expected);
  }

  // Rasterization matches the pixel-sampled circle for arbitrary sub-pixel
  // positions and radii, including dots clipped by the image border.
  {
    constexpr int kWidth = 23;
    constexpr int kHeight = 17;
    std::mt19937 random(7);
    std::uniform_real_distribution<double> x_position(-1.0, kWidth + 1.0);
    std::uniform_real_distribution<double> y_position(-1.0, kHeight + 1.0);
    std::uniform_real_distribution<double> radius(0.0, 4.0);

    for (int trial = 0; trial < 2000; ++trial) {
      const stippling::Dot dot{x_position(random), y_position(random), radius(random)};
      stippling::RasterGrid grid(kWidth, kHeight);
      grid.draw_dot(dot);
      CHECK(grid.pixels() == brute_force_render({dot}, kWidth, kHeight));
    }
  }

  // Incremental squared error stays exact across a long random edit sequence.
  {
    constexpr int kSize = 24;
    std::mt19937 random(11);
    std::uniform_real_distribution<double> position(0.0, kSize);
    std::uniform_real_distribution<double> radius(stippling::kMinDotRadius,
                                                  stippling::kMaxDotRadius);
    std::uniform_int_distribution<int> shade(0, 255);
    std::vector<std::uint8_t> target(kSize * kSize);
    for (auto& value : target) {
      value = static_cast<std::uint8_t>(shade(random));
    }

    std::vector<stippling::Dot> dots(40);
    stippling::RasterGrid grid(kSize, kSize);
    for (auto& dot : dots) {
      dot = {position(random), position(random), radius(random)};
      grid.draw_dot(dot);
    }

    auto error = grid.squared_error(target);
    for (int step = 0; step < 500; ++step) {
      auto& dot = dots[static_cast<std::size_t>(step) % dots.size()];
      const stippling::Dot next{position(random), position(random), radius(random)};
      error = grid.apply_dot_delta_and_update_error(dot, next, target, error);
      dot = next;
    }

    CHECK(grid.pixels() == brute_force_render(dots, kSize, kSize));
    CHECK(error == grid.squared_error(target));
  }

  return 0;
}
