#pragma once

// Public native engine surface shared by the CLI, C ABI, and WASM runtime.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stippling/engine/dot.hpp"
#include "stippling/engine/export.hpp"

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

/** Supported pixel formats for engine image buffers. */
enum class PixelFormat {
  grayscale8,
  rgba8,
};

/** High-level engine lifecycle states. */
enum class EngineStatus {
  booting,
  idle,
  configured,
  image_loaded,
};

/** Feature flags surfaced to the browser and CLI runtimes. */
struct EngineCapabilities {
  bool incremental_fitness{false};
  bool multiscale{false};
  bool benchmark_mode{false};
  bool export_svg{false};
  bool export_png{false};
};

/** Run configuration shared by native, CLI, and WASM entrypoints. */
struct EngineConfig {
  std::uint32_t population_size{100};
  double mutation_rate{0.2};
  std::uint32_t dot_count{0};
  double elitism_ratio{0.15};
  std::uint32_t seed{1};
  std::uint32_t generations_per_batch{1};
};

/** Preprocessing controls used to prepare an optimization target. */
struct TargetProcessingConfig {
  std::uint32_t blur_amount{0};
  std::uint32_t threshold{130};
  std::uint32_t max_dot_count{200000};
};

/** Summary statistics derived from the prepared target image. */
struct TargetStats {
  std::uint32_t black_pixels{0};
  std::uint32_t total_pixels{0};
  double black_percentage{0.0};
  std::uint32_t recommended_dot_count{0};
};

/** Owning image buffer passed into and out of the engine. */
struct ImageBuffer {
  PixelFormat format{PixelFormat::grayscale8};
  int width{0};
  int height{0};
  std::vector<std::uint8_t> pixels{};

  [[nodiscard]] bool valid() const noexcept;
};

/**
 * Shared engine surface for the future WASM build and the native CLI.
 * This starts as a small skeleton so the migration can grow around a stable,
 * documented API instead of a one-off translation from TypeScript classes.
 */
class Engine {
 public:
  Engine();
  ~Engine();

  /** Returns static feature flags for this engine build. */
  [[nodiscard]] const EngineCapabilities& capabilities() const noexcept;
  /** Returns the current engine lifecycle state. */
  [[nodiscard]] EngineStatus status() const noexcept;
  /** Returns the active optimizer configuration. */
  [[nodiscard]] const EngineConfig& config() const noexcept;
  /** Returns the prepared preview image. */
  [[nodiscard]] const ImageBuffer& image() const noexcept;
  /** Returns summary statistics for the prepared target. */
  [[nodiscard]] const TargetStats& target_stats() const noexcept;
  /** Reports whether a prepared image is currently loaded. */
  [[nodiscard]] bool has_image() const noexcept;
  /** Reports whether the optimizer has been initialized. */
  [[nodiscard]] bool has_optimizer() const noexcept;

  /** Stores a new engine configuration and clears derived optimizer state. */
  void configure(const EngineConfig& config);
  /** Stores a raw image without preparing an optimization target. */
  void load_image(ImageBuffer image);
  /** Preprocesses the source image into the engine's prepared target state. */
  [[nodiscard]] ImageBuffer prepare_target(
      const ImageBuffer& source_image,
      const TargetProcessingConfig& config);
  /** Builds the multiscale pyramid and initializes the coarsest optimizer. */
  void initialize_optimizer();
  /** Advances the active optimizer by one configured batch. */
  [[nodiscard]] OptimizerProgress evolve_batch();
  /** Returns the current best dots in full-image coordinates. */
  [[nodiscard]] const std::vector<Dot>& best_dots() const;
  /** Returns whole-run optimizer progress metrics. */
  [[nodiscard]] OptimizerProgress optimizer_progress() const;
  /** Validates incremental raster bookkeeping against a reference redraw. */
  [[nodiscard]] OptimizerValidation validate_optimizer() const;
  /** Exports the current best result as SVG text. */
  [[nodiscard]] std::string export_best_svg(int scale = 1) const;
  /** Exports the current best result as PNG bytes. */
  [[nodiscard]] std::vector<std::uint8_t> export_best_png(int scale = 1) const;
  /** Renders the current best result as a grayscale raster. */
  [[nodiscard]] std::vector<std::uint8_t> render_best_grayscale(
      int scale = 1) const;
  /** Computes quality metrics for the current best result. */
  [[nodiscard]] QualityMetrics best_quality_metrics() const;

  /** Returns the current engine state as a string. */
  [[nodiscard]] std::string status_string() const;

 private:
  struct PyramidLevel {
    // Each level stores a resampled target + importance map. The optimizer runs
    // independently per level and promotes only the best dots upward.
    int width{0};
    int height{0};
    std::vector<std::uint8_t> target{};
    std::vector<double> importance{};
  };

  EngineCapabilities capabilities_{};
  EngineConfig config_{};
  ImageBuffer image_{};
  TargetStats target_stats_{};
  std::vector<std::uint8_t> optimizer_target_{};
  std::vector<double> importance_map_{};
  std::vector<PyramidLevel> pyramid_{};
  std::size_t current_level_index_{0};
  std::uint32_t total_generations_{0};
  std::unique_ptr<Optimizer> optimizer_{};
  // When the optimizer is still working at a coarse pyramid level we project
  // the current best dots back into full-image coordinates so the UI, exports,
  // and parity tools can still observe meaningful output.
  mutable std::vector<Dot> projected_best_dots_{};
  EngineStatus status_{EngineStatus::booting};

  [[nodiscard]] std::vector<Dot> project_dots_to_image_space(
      const std::vector<Dot>& dots,
      int source_width,
      int source_height) const;

  // Rebuild the optimizer for the next pyramid level once the current level has
  // either converged enough or stalled long enough to justify promotion.
  void initialize_level_optimizer(const std::vector<Dot>& seed_dots);
  void maybe_promote_level();
};

[[nodiscard]] std::string to_string(EngineStatus status);

}  // namespace stippling
