#include "stippling/engine/engine.hpp"
#include "stippling/engine/export.hpp"

#include "check.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

stippling::ImageBuffer make_source_image() {
  return {
      .format = stippling::PixelFormat::rgba8,
      .width = 4,
      .height = 4,
      .pixels = std::vector<std::uint8_t>{
          0,   0,   0,   255, 32,  32,  32,  255, 255, 255, 255, 255, 255, 255, 255, 255,
          0,   0,   0,   255, 32,  32,  32,  255, 255, 255, 255, 255, 255, 255, 255, 255,
          255, 255, 255, 255, 32,  32,  32,  255, 0,   0,   0,   255, 255, 255, 255, 255,
          255, 255, 255, 255, 32,  32,  32,  255, 0,   0,   0,   255, 255, 255, 255, 255,
      },
  };
}

stippling::EngineConfig make_config() {
  return {
      .population_size = 10,
      .mutation_rate = 0.2,
      .dot_count = 12,
      .elitism_ratio = 0.2,
      .seed = 7,
      .generations_per_batch = 2,
  };
}

std::uint32_t read_png_dimension(const std::vector<std::uint8_t>& png,
                                 std::size_t offset) {
  return (static_cast<std::uint32_t>(png[offset]) << 24u) |
         (static_cast<std::uint32_t>(png[offset + 1]) << 16u) |
         (static_cast<std::uint32_t>(png[offset + 2]) << 8u) |
         static_cast<std::uint32_t>(png[offset + 3]);
}

/**
 * Decodes the 1-bit grayscale PNGs this engine writes (stored DEFLATE blocks,
 * filter type 0) back into a 0/255 raster.
 */
std::vector<std::uint8_t> decode_binary_png(const std::vector<std::uint8_t>& png,
                                            std::uint32_t width,
                                            std::uint32_t height) {
  CHECK(png[24] == 1u);  // bit depth
  CHECK(png[25] == 0u);  // grayscale
  std::vector<std::uint8_t> zlib;
  for (std::size_t offset = 8; offset < png.size();) {
    const auto length = read_png_dimension(png, offset);
    const std::string type(png.begin() + static_cast<long>(offset + 4),
                           png.begin() + static_cast<long>(offset + 8));
    if (type == "IDAT") {
      zlib.insert(zlib.end(), png.begin() + static_cast<long>(offset + 8),
                  png.begin() + static_cast<long>(offset + 8 + length));
    }
    offset += 12u + length;
  }

  std::vector<std::uint8_t> scanlines;
  for (std::size_t offset = 2; offset + 4 < zlib.size();) {
    const auto block_size = static_cast<std::size_t>(zlib[offset + 1] | (zlib[offset + 2] << 8));
    scanlines.insert(scanlines.end(), zlib.begin() + static_cast<long>(offset + 5),
                     zlib.begin() + static_cast<long>(offset + 5 + block_size));
    offset += 5u + block_size;
  }

  const auto row_bytes = (width + 7u) / 8u;
  CHECK(scanlines.size() == height * (row_bytes + 1u));
  std::vector<std::uint8_t> raster(static_cast<std::size_t>(width) * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    CHECK(scanlines[y * (row_bytes + 1u)] == 0u);
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto byte = scanlines[y * (row_bytes + 1u) + 1u + x / 8u];
      raster[y * width + x] = (byte & (0x80u >> (x % 8u))) != 0u ? 255u : 0u;
    }
  }
  return raster;
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  for (auto at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

int main() {
  stippling::Engine engine;
  engine.configure(make_config());
  (void)engine.prepare_target(make_source_image(), {});
  engine.initialize_optimizer();
  (void)engine.evolve_batch();

  const auto validation = engine.validate_optimizer();
  CHECK(validation.valid);
  CHECK(validation.checked_candidates == make_config().population_size);
  CHECK(validation.mismatched_candidates == 0);

  const auto svg = engine.export_best_svg(2);
  CHECK(svg.find("<svg") != std::string::npos);
  CHECK(count_occurrences(svg, "<circle") == engine.best_dots().size());

  const auto png = engine.export_best_png(2);
  CHECK(png.size() > 32);
  CHECK(std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
  CHECK(read_png_dimension(png, 16) == 8);
  CHECK(read_png_dimension(png, 20) == 8);
  // The PNG holds exactly the rendered black/white raster, losslessly.
  CHECK(decode_binary_png(png, 8, 8) == engine.render_best_grayscale(2));

  // Odd widths exercise the partial last byte of each packed scanline.
  {
    const std::vector<stippling::Dot> dots{{1.2, 1.7, 1.3}, {9.6, 3.1, 0.9}, {4.5, 6.5, 1.35}};
    const auto odd_png = stippling::export_dots_to_png(dots, 11, 7, 3);
    CHECK(decode_binary_png(odd_png, 33, 21) ==
          stippling::render_dots_to_grayscale(dots, 11, 7, 3));
  }

  // Quality metrics compare against the optimizer's grayscale target.
  const auto quality = engine.best_quality_metrics();
  CHECK(quality.mse > 0.0);

  // Timelapse frames stay bounded and evenly spaced over long runs.
  {
    auto config = make_config();
    config.generations_per_batch = 1;
    config.timelapse_max_frames = 8;
    stippling::Engine long_run;
    long_run.configure(config);
    (void)long_run.prepare_target(make_source_image(), {});
    long_run.initialize_optimizer();
    for (int generation = 0; generation < 100; ++generation) {
      (void)long_run.evolve_batch();
    }

    const auto& frames = long_run.timelapse_frames();
    CHECK(!frames.empty());
    CHECK(frames.size() <= config.timelapse_max_frames);
    CHECK(frames.front().generation == 0);
    const auto stride = frames[1].generation - frames[0].generation;
    CHECK(stride >= 100 / config.timelapse_max_frames);
    for (std::size_t index = 1; index < frames.size(); ++index) {
      CHECK(frames[index].generation - frames[index - 1].generation == stride);
    }

    // The export appends the final generation and shows one frame at a time:
    // every frame animates over the full loop and is visible only in its slot.
    const auto timelapse = long_run.export_timelapse_svg(1, 100);
    const auto frame_count = count_occurrences(timelapse, "<g ");
    CHECK(frame_count == frames.size() + 1);
    CHECK(count_occurrences(timelapse, "<animate ") == frame_count);
    CHECK(count_occurrences(timelapse, "calcMode=\"discrete\"") == frame_count);
    CHECK(count_occurrences(timelapse, "repeatCount=\"indefinite\"") == frame_count);
    CHECK(count_occurrences(timelapse, "<set ") == 0);
    CHECK(timelapse.find("data-generation=\"100\"") != std::string::npos);
    CHECK(timelapse.find("values=\"1;0\"") != std::string::npos);
    CHECK(timelapse.find("values=\"0;1\"") != std::string::npos);
    CHECK(count_occurrences(timelapse, "values=\"0;1;0\"") == frame_count - 2);
  }

  // Disabling capture still exports the final state as a single static frame.
  {
    auto config = make_config();
    config.timelapse_max_frames = 0;
    stippling::Engine no_frames;
    no_frames.configure(config);
    (void)no_frames.prepare_target(make_source_image(), {});
    no_frames.initialize_optimizer();
    (void)no_frames.evolve_batch();
    CHECK(no_frames.timelapse_frames().empty());
    const auto timelapse = no_frames.export_timelapse_svg(1, 100);
    CHECK(count_occurrences(timelapse, "<g ") == 1);
    CHECK(count_occurrences(timelapse, "<animate ") == 0);
  }

  return 0;
}
