#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stippling/engine/dot.hpp"
#include "stippling/engine/export.hpp"
#include "stippling/engine/worker_pool.hpp"

namespace stippling {

class Optimizer;

struct OptimizerProgress {
  std::uint32_t generation{0};
  double best_fitness{0.0};
  std::uint64_t best_squared_error{0};
};

/** Result of validating incremental raster state against a full redraw. */
struct OptimizerValidation {
  bool valid{true};
  std::uint32_t checked_candidates{0};
  std::uint32_t mismatched_candidates{0};
  std::uint32_t first_mismatch_index{0};
  std::uint64_t max_squared_error_delta{0};
  std::uint32_t total_pixel_mismatches{0};
};

enum class PixelFormat {
  grayscale8,
  rgba8,
};

struct EngineConfig {
  std::uint32_t population_size{100};
  double mutation_rate{0.2};
  std::uint32_t dot_count{0};
  double elitism_ratio{0.15};
  std::uint32_t seed{1};
  std::uint32_t generations_per_batch{1};
  // When full, every other frame is dropped so frames stay evenly spaced.
  std::uint32_t timelapse_max_frames{60};
  // Threads used to breed children. Results are the same for any value.
  std::uint32_t thread_count{1};
};

struct TargetProcessingConfig {
  std::uint32_t blur_amount{0};
  std::uint32_t threshold{130};
  std::uint32_t max_dot_count{200000};
};

struct TargetStats {
  std::uint32_t black_pixels{0};
  std::uint32_t total_pixels{0};
  double black_percentage{0.0};
  std::uint32_t recommended_dot_count{0};
};

struct ImageBuffer {
  PixelFormat format{PixelFormat::grayscale8};
  int width{0};
  int height{0};
  std::vector<std::uint8_t> pixels{};

  [[nodiscard]] bool valid() const noexcept;
};

/** Preprocessing, multiscale search, and export; used by the CLI and the C ABI. */
class Engine {
 public:
  Engine();
  ~Engine();

  [[nodiscard]] const EngineConfig& config() const noexcept;
  /** The thresholded black/white preview, not the optimizer's grayscale target. */
  [[nodiscard]] const ImageBuffer& image() const noexcept;
  [[nodiscard]] const TargetStats& target_stats() const noexcept;
  [[nodiscard]] bool has_image() const noexcept;
  [[nodiscard]] bool has_optimizer() const noexcept;

  /** Also discards any run in progress. */
  void configure(const EngineConfig& config);
  /** Also discards any run in progress. Returns the preview image. */
  [[nodiscard]] ImageBuffer prepare_target(
      const ImageBuffer& source_image,
      const TargetProcessingConfig& config);
  /** Builds the resolution pyramid and starts on its coarsest level. */
  void initialize_optimizer();
  [[nodiscard]] OptimizerProgress evolve_batch();
  /** Always in full-image coordinates, even while searching a coarse level. */
  [[nodiscard]] std::vector<Dot> best_dots() const;
  /** `generation` counts the whole run, not just the current level. */
  [[nodiscard]] OptimizerProgress optimizer_progress() const;
  [[nodiscard]] OptimizerValidation validate_optimizer() const;
  [[nodiscard]] std::string export_best_svg(int scale = 1) const;
  /** Ends with the current best, even if the capture stride skipped it. */
  [[nodiscard]] std::string export_timelapse_svg(
      int scale = 1,
      std::uint32_t frame_duration_ms = 120) const;
  [[nodiscard]] const std::vector<TimelapseFrame>& timelapse_frames()
      const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> export_best_png(int scale = 1) const;
  [[nodiscard]] std::vector<std::uint8_t> render_best_grayscale(
      int scale = 1) const;
  /** Measured against the grayscale target the optimizer minimizes error to. */
  [[nodiscard]] QualityMetrics best_quality_metrics() const;

 private:
  struct PyramidLevel {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> target{};
    std::vector<double> importance{};
  };

  EngineConfig config_{};
  ImageBuffer image_{};
  TargetStats target_stats_{};
  std::vector<std::uint8_t> optimizer_target_{};
  std::vector<double> importance_map_{};
  std::vector<PyramidLevel> pyramid_{};
  std::size_t current_level_index_{0};
  std::uint32_t total_generations_{0};
  // Declared before optimizer_ so the optimizer is destroyed first.
  std::unique_ptr<WorkerPool> pool_{};
  std::unique_ptr<Optimizer> optimizer_{};
  std::vector<TimelapseFrame> timelapse_frames_{};
  std::uint32_t timelapse_stride_{1};

  [[nodiscard]] std::vector<Dot> project_dots_to_image_space(
      const std::vector<Dot>& dots,
      int source_width,
      int source_height) const;

  void initialize_level_optimizer(const std::vector<Dot>& seed_dots);
  void maybe_promote_level();
  void reset_run_state();
  void maybe_capture_timelapse_frame();
};

}  // namespace stippling
