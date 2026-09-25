#pragma once

#include <algorithm>

namespace stippling {

/**
 * One stipple dot: a filled circle centered at (x, y) in pixel coordinates,
 * where pixel (px, py) spans [px, px + 1) x [py, py + 1).
 */
struct Dot {
  double x{0.0};
  double y{0.0};
  double radius{1.0};
};

// Shared by the optimizer, multiscale projection, and benchmarks.
inline constexpr double kMinDotRadius = 0.35;
inline constexpr double kMaxDotRadius = 1.35;

inline double clamp_dot_radius(double radius) {
  return std::clamp(radius, kMinDotRadius, kMaxDotRadius);
}

}  // namespace stippling
