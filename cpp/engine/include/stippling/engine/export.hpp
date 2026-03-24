#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "stippling/engine/dot.hpp"

namespace stippling {

struct QualityMetrics {
  double mse{0.0};
  double rmse{0.0};
  double psnr{0.0};
  double exact_pixel_ratio{0.0};
};

struct TimelapseFrame {
  std::uint32_t generation{0};
  std::vector<Dot> dots{};
};

std::vector<std::uint8_t> render_dots_to_grayscale(const std::vector<Dot>& dots,
                                                   int width,
                                                   int height,
                                                   int scale);
std::vector<std::uint8_t> render_dots_to_rgba(const std::vector<Dot>& dots,
                                              int width,
                                              int height,
                                              int scale);
std::string export_dots_to_svg(const std::vector<Dot>& dots,
                               int width,
                               int height,
                               int scale);
std::string export_timelapse_to_svg(const std::vector<TimelapseFrame>& frames,
                                    int width,
                                    int height,
                                    int scale,
                                    std::uint32_t frame_duration_ms);
std::vector<std::uint8_t> export_dots_to_png(const std::vector<Dot>& dots,
                                             int width,
                                             int height,
                                             int scale);
QualityMetrics compute_quality_metrics(
    const std::vector<std::uint8_t>& target,
    const std::vector<std::uint8_t>& rendered);

}  // namespace stippling
