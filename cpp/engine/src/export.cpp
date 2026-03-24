#include "stippling/engine/export.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "stippling/engine/raster_grid.hpp"

namespace stippling {

namespace {

constexpr std::array<std::uint8_t, 8> kPngSignature{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
};

std::uint32_t clamp_scale(int scale) {
  if (scale <= 0) {
    throw std::invalid_argument("Export scale must be positive");
  }

  return static_cast<std::uint32_t>(scale);
}

std::uint32_t crc32(std::string_view type, const std::vector<std::uint8_t>& data) {
  std::uint32_t crc = 0xffffffffu;
  auto update = [&crc](std::uint8_t byte) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) != 0u ? 0xedb88320u ^ (crc >> 1u) : (crc >> 1u);
    }
  };

  for (const auto character : type) {
    update(static_cast<std::uint8_t>(character));
  }
  for (const auto byte : data) {
    update(byte);
  }
  return crc ^ 0xffffffffu;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
  std::uint32_t a = 1u;
  std::uint32_t b = 0u;

  for (const auto byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }

  return (b << 16u) | a;
}

void append_u32_be(std::vector<std::uint8_t>* output, std::uint32_t value) {
  output->push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
  output->push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  output->push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  output->push_back(static_cast<std::uint8_t>(value & 0xffu));
}

void append_chunk(std::vector<std::uint8_t>* output,
                  std::string_view type,
                  const std::vector<std::uint8_t>& data) {
  append_u32_be(output, static_cast<std::uint32_t>(data.size()));
  output->insert(output->end(), type.begin(), type.end());
  output->insert(output->end(), data.begin(), data.end());
  append_u32_be(output, crc32(type, data));
}

std::vector<std::uint8_t> make_png_image_data(const std::vector<std::uint8_t>& rgba,
                                              int width,
                                              int height) {
  const auto row_stride = static_cast<std::size_t>(width) * 4u;
  std::vector<std::uint8_t> filtered;
  filtered.reserve(static_cast<std::size_t>(height) * (row_stride + 1u));

  for (int y = 0; y < height; ++y) {
    filtered.push_back(0u);
    const auto row_start = static_cast<std::size_t>(y) * row_stride;
    filtered.insert(filtered.end(), rgba.begin() + static_cast<long>(row_start),
                    rgba.begin() + static_cast<long>(row_start + row_stride));
  }

  std::vector<std::uint8_t> compressed;
  compressed.push_back(0x78u);
  compressed.push_back(0x01u);

  std::size_t offset = 0;
  while (offset < filtered.size()) {
    const auto remaining = filtered.size() - offset;
    const auto block_size =
        static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535u));
    const auto is_final = offset + block_size == filtered.size();

    compressed.push_back(is_final ? 0x01u : 0x00u);
    compressed.push_back(static_cast<std::uint8_t>(block_size & 0xffu));
    compressed.push_back(static_cast<std::uint8_t>((block_size >> 8u) & 0xffu));
    const auto inverted = static_cast<std::uint16_t>(~block_size);
    compressed.push_back(static_cast<std::uint8_t>(inverted & 0xffu));
    compressed.push_back(static_cast<std::uint8_t>((inverted >> 8u) & 0xffu));
    compressed.insert(compressed.end(), filtered.begin() + static_cast<long>(offset),
                      filtered.begin() + static_cast<long>(offset + block_size));
    offset += block_size;
  }

  append_u32_be(&compressed, adler32(filtered));
  return compressed;
}

std::vector<std::uint8_t> encode_png_rgba(const std::vector<std::uint8_t>& rgba,
                                          int width,
                                          int height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("PNG dimensions must be positive");
  }
  if (rgba.size() != static_cast<std::size_t>(width * height * 4)) {
    throw std::invalid_argument("RGBA buffer size does not match PNG dimensions");
  }

  std::vector<std::uint8_t> output(kPngSignature.begin(), kPngSignature.end());
  std::vector<std::uint8_t> ihdr;
  ihdr.reserve(13);
  append_u32_be(&ihdr, static_cast<std::uint32_t>(width));
  append_u32_be(&ihdr, static_cast<std::uint32_t>(height));
  ihdr.push_back(8u);
  ihdr.push_back(6u);
  ihdr.push_back(0u);
  ihdr.push_back(0u);
  ihdr.push_back(0u);
  append_chunk(&output, "IHDR", ihdr);

  append_chunk(&output, "IDAT", make_png_image_data(rgba, width, height));
  append_chunk(&output, "IEND", {});
  return output;
}

std::string format_dot_svg(const Dot& dot, std::uint32_t scale) {
  std::ostringstream stream;
  stream << "<circle cx=\"" << dot.x * static_cast<double>(scale)
         << "\" cy=\"" << dot.y * static_cast<double>(scale)
         << "\" r=\"" << dot.radius * static_cast<double>(scale)
         << "\" fill=\"black\" />";
  return stream.str();
}

}  // namespace

std::vector<std::uint8_t> render_dots_to_grayscale(const std::vector<Dot>& dots,
                                                   int width,
                                                   int height,
                                                   int scale) {
  const auto resolved_scale = clamp_scale(scale);
  RasterGrid grid(width * static_cast<int>(resolved_scale),
                  height * static_cast<int>(resolved_scale));

  // Export rendering stays binary so the PNG/SVG outputs match the optimizer's
  // own binary target representation rather than adding an unrelated antialiasing
  // layer on top of the engine.
  for (const auto& dot : dots) {
    grid.draw_dot({
        .x = dot.x * static_cast<double>(resolved_scale),
        .y = dot.y * static_cast<double>(resolved_scale),
        .radius = dot.radius * static_cast<double>(resolved_scale),
    });
  }

  return grid.pixels();
}

std::vector<std::uint8_t> render_dots_to_rgba(const std::vector<Dot>& dots,
                                              int width,
                                              int height,
                                              int scale) {
  const auto grayscale = render_dots_to_grayscale(dots, width, height, scale);
  std::vector<std::uint8_t> rgba(grayscale.size() * 4u, 255u);

  for (std::size_t index = 0; index < grayscale.size(); ++index) {
    rgba[index * 4u] = grayscale[index];
    rgba[index * 4u + 1u] = grayscale[index];
    rgba[index * 4u + 2u] = grayscale[index];
  }

  return rgba;
}

std::string export_dots_to_svg(const std::vector<Dot>& dots,
                               int width,
                               int height,
                               int scale) {
  const auto resolved_scale = clamp_scale(scale);
  std::ostringstream stream;
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
         << width * static_cast<int>(resolved_scale) << ' '
         << height * static_cast<int>(resolved_scale) << "\" width=\""
         << width * static_cast<int>(resolved_scale) << "\" height=\""
         << height * static_cast<int>(resolved_scale) << "\">";
  stream << "<rect width=\"100%\" height=\"100%\" fill=\"white\" />";
  for (const auto& dot : dots) {
    stream << format_dot_svg(dot, resolved_scale);
  }
  stream << "</svg>";
  return stream.str();
}

std::string export_timelapse_to_svg(const std::vector<TimelapseFrame>& frames,
                                    int width,
                                    int height,
                                    int scale,
                                    std::uint32_t frame_duration_ms) {
  const auto resolved_scale = clamp_scale(scale);
  if (frames.empty()) {
    throw std::invalid_argument("Timelapse export requires at least one frame");
  }

  const auto total_duration_ms =
      std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(frames.size())) *
      std::max<std::uint32_t>(1u, frame_duration_ms);
  std::ostringstream stream;
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
         << width * static_cast<int>(resolved_scale) << ' '
         << height * static_cast<int>(resolved_scale) << "\" width=\""
         << width * static_cast<int>(resolved_scale) << "\" height=\""
         << height * static_cast<int>(resolved_scale) << "\">";
  stream << "<rect width=\"100%\" height=\"100%\" fill=\"white\" />";
  for (std::size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const auto start_ms = static_cast<std::uint32_t>(frame_index) * frame_duration_ms;
    stream << "<g opacity=\"0\">";
    stream << "<set attributeName=\"opacity\" to=\"1\" begin=\"0ms;"
           << total_duration_ms << "ms\" dur=\"" << frame_duration_ms
           << "ms\" repeatCount=\"indefinite\" />";
    stream << "<set attributeName=\"opacity\" to=\"1\" begin=\"" << start_ms
           << "ms\" dur=\"" << frame_duration_ms
           << "ms\" repeatCount=\"indefinite\" />";
    for (const auto& dot : frames[frame_index].dots) {
      stream << format_dot_svg(dot, resolved_scale);
    }
    stream << "</g>";
  }
  stream << "</svg>";
  return stream.str();
}

std::vector<std::uint8_t> export_dots_to_png(const std::vector<Dot>& dots,
                                             int width,
                                             int height,
                                             int scale) {
  const auto resolved_scale = clamp_scale(scale);
  return encode_png_rgba(render_dots_to_rgba(dots, width, height, scale),
                         width * static_cast<int>(resolved_scale),
                         height * static_cast<int>(resolved_scale));
}

QualityMetrics compute_quality_metrics(const std::vector<std::uint8_t>& target,
                                       const std::vector<std::uint8_t>& rendered) {
  if (target.size() != rendered.size()) {
    throw std::invalid_argument("Quality metric buffers must have the same size");
  }
  if (target.empty()) {
    return {};
  }

  double squared_error_sum = 0.0;
  std::size_t exact_matches = 0;
  for (std::size_t index = 0; index < target.size(); ++index) {
    const auto diff =
        static_cast<double>(static_cast<int>(rendered[index]) - static_cast<int>(target[index]));
    squared_error_sum += diff * diff;
    if (rendered[index] == target[index]) {
      ++exact_matches;
    }
  }

  const auto mse = squared_error_sum / static_cast<double>(target.size());
  const auto rmse = std::sqrt(mse);
  const auto psnr = mse == 0.0
                        ? std::numeric_limits<double>::infinity()
                        : 20.0 * std::log10(255.0) - 10.0 * std::log10(mse);

  return {
      .mse = mse,
      .rmse = rmse,
      .psnr = psnr,
      .exact_pixel_ratio =
          static_cast<double>(exact_matches) / static_cast<double>(target.size()),
  };
}

}  // namespace stippling
