#include "stippling/engine/export.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
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

// Stored (uncompressed) DEFLATE blocks: simple and deterministic.
std::vector<std::uint8_t> zlib_store(const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> compressed;
  compressed.reserve(data.size() + data.size() / 65535u * 5u + 11u);
  compressed.push_back(0x78u);
  compressed.push_back(0x01u);

  std::size_t offset = 0;
  while (offset < data.size()) {
    const auto remaining = data.size() - offset;
    const auto block_size =
        static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535u));
    const auto is_final = offset + block_size == data.size();

    compressed.push_back(is_final ? 0x01u : 0x00u);
    compressed.push_back(static_cast<std::uint8_t>(block_size & 0xffu));
    compressed.push_back(static_cast<std::uint8_t>((block_size >> 8u) & 0xffu));
    const auto inverted = static_cast<std::uint16_t>(~block_size);
    compressed.push_back(static_cast<std::uint8_t>(inverted & 0xffu));
    compressed.push_back(static_cast<std::uint8_t>((inverted >> 8u) & 0xffu));
    compressed.insert(compressed.end(), data.begin() + static_cast<long>(offset),
                      data.begin() + static_cast<long>(offset + block_size));
    offset += block_size;
  }

  append_u32_be(&compressed, adler32(data));
  return compressed;
}

// Stipple output is strictly black/white (0 = black), so 1-bit is lossless and
// 32x smaller than RGBA.
std::vector<std::uint8_t> encode_png_binary(const std::vector<std::uint8_t>& raster,
                                            int width,
                                            int height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("PNG dimensions must be positive");
  }
  if (raster.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    throw std::invalid_argument("Raster size does not match PNG dimensions");
  }

  // Each scanline: filter type 0, then pixels packed MSB-first, 1 = white.
  const auto row_bytes = (static_cast<std::size_t>(width) + 7u) / 8u;
  std::vector<std::uint8_t> scanlines(static_cast<std::size_t>(height) * (row_bytes + 1u), 0u);
  for (int y = 0; y < height; ++y) {
    auto* row = scanlines.data() + static_cast<std::size_t>(y) * (row_bytes + 1u) + 1u;
    for (int x = 0; x < width; ++x) {
      if (raster[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)] != 0u) {
        row[x / 8] |= static_cast<std::uint8_t>(0x80u >> (x % 8));
      }
    }
  }

  std::vector<std::uint8_t> output(kPngSignature.begin(), kPngSignature.end());
  std::vector<std::uint8_t> ihdr;
  ihdr.reserve(13);
  append_u32_be(&ihdr, static_cast<std::uint32_t>(width));
  append_u32_be(&ihdr, static_cast<std::uint32_t>(height));
  ihdr.push_back(1u);  // bit depth
  ihdr.push_back(0u);  // color type: grayscale
  ihdr.push_back(0u);  // compression
  ihdr.push_back(0u);  // filter
  ihdr.push_back(0u);  // interlace
  append_chunk(&output, "IHDR", ihdr);

  append_chunk(&output, "IDAT", zlib_store(scanlines));
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

  for (const auto& dot : dots) {
    grid.draw_dot({
        .x = dot.x * static_cast<double>(resolved_scale),
        .y = dot.y * static_cast<double>(resolved_scale),
        .radius = dot.radius * static_cast<double>(resolved_scale),
    });
  }

  return grid.pixels();
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

  const auto frame_count = frames.size();
  const auto resolved_frame_duration_ms = std::max<std::uint32_t>(1u, frame_duration_ms);
  const auto total_duration_ms =
      static_cast<std::uint64_t>(frame_count) * resolved_frame_duration_ms;
  std::ostringstream stream;
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
         << width * static_cast<int>(resolved_scale) << ' '
         << height * static_cast<int>(resolved_scale) << "\" width=\""
         << width * static_cast<int>(resolved_scale) << "\" height=\""
         << height * static_cast<int>(resolved_scale) << "\">";
  stream << "<rect width=\"100%\" height=\"100%\" fill=\"white\" />";

  // Each frame gets one discrete opacity animation over the whole loop: it is
  // visible only during its own slot. (A <set> with repeatCount="indefinite"
  // never ends, so frames would pile up on top of each other.)
  const auto key_time = [&](std::size_t frame_index) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(6)
          << static_cast<double>(frame_index) / static_cast<double>(frame_count);
    return value.str();
  };
  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    const auto is_first = frame_index == 0;
    const auto is_last = frame_index + 1 == frame_count;
    stream << "<g data-generation=\"" << frames[frame_index].generation << "\"";
    if (frame_count > 1) {
      std::string key_times;
      std::string values;
      if (is_first) {
        key_times = "0;" + key_time(1);
        values = "1;0";
      } else if (is_last) {
        key_times = "0;" + key_time(frame_index);
        values = "0;1";
      } else {
        key_times = "0;" + key_time(frame_index) + ";" + key_time(frame_index + 1);
        values = "0;1;0";
      }
      stream << " opacity=\"" << (is_first ? 1 : 0) << "\">";
      stream << "<animate attributeName=\"opacity\" calcMode=\"discrete\" dur=\""
             << total_duration_ms << "ms\" repeatCount=\"indefinite\" keyTimes=\""
             << key_times << "\" values=\"" << values << "\" />";
    } else {
      stream << ">";
    }
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
  return encode_png_binary(render_dots_to_grayscale(dots, width, height, scale),
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
  for (std::size_t index = 0; index < target.size(); ++index) {
    const auto diff =
        static_cast<double>(static_cast<int>(rendered[index]) - static_cast<int>(target[index]));
    squared_error_sum += diff * diff;
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
  };
}

}  // namespace stippling
