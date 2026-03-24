#pragma once

// Public export/render surface shared by the native CLI, the C++ engine, and
// the WASM worker bridge.

#include <cstdint>
#include <string>
#include <vector>

#include "stippling/engine/dot.hpp"

namespace stippling {

/** Aggregate image-quality metrics for one rendered result. */
struct QualityMetrics {
  double mse{0.0};
  double rmse{0.0};
  double psnr{0.0};
  double exact_pixel_ratio{0.0};
};

/** One captured frame of a timelapse export. */
struct TimelapseFrame {
  std::uint32_t generation{0};
  std::vector<Dot> dots{};
};

/** Renders dots into the binary grayscale raster used by the engine. */
std::vector<std::uint8_t> render_dots_to_grayscale(const std::vector<Dot>& dots,
                                                   int width,
                                                   int height,
                                                   int scale);
/** Renders dots into an RGBA raster for PNG encoding. */
std::vector<std::uint8_t> render_dots_to_rgba(const std::vector<Dot>& dots,
                                              int width,
                                              int height,
                                              int scale);
/** Serializes dots as a standalone SVG image. */
std::string export_dots_to_svg(const std::vector<Dot>& dots,
                               int width,
                               int height,
                               int scale);
/** Serializes a captured timelapse as an animated SVG document. */
std::string export_timelapse_to_svg(const std::vector<TimelapseFrame>& frames,
                                    int width,
                                    int height,
                                    int scale,
                                    std::uint32_t frame_duration_ms);
/** Serializes dots as a PNG byte stream. */
std::vector<std::uint8_t> export_dots_to_png(const std::vector<Dot>& dots,
                                             int width,
                                             int height,
                                             int scale);
/** Computes basic quality metrics between a target and rendered raster. */
QualityMetrics compute_quality_metrics(
    const std::vector<std::uint8_t>& target,
    const std::vector<std::uint8_t>& rendered);

}  // namespace stippling
