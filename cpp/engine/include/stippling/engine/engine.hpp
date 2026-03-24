#pragma once

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

enum class EngineStatus {
  booting,
  idle,
  configured,
  image_loaded,
};

struct EngineCapabilities {
  bool incremental_fitness{false};
  bool multiscale{false};
  bool benchmark_mode{false};
  bool export_svg{false};
  bool export_png{false};
};

struct EngineConfig {
  std::uint32_t population_size{100};
  double mutation_rate{0.2};
  std::uint32_t dot_count{0};
  double elitism_ratio{0.15};
  std::uint32_t seed{1};
  std::uint32_t generations_per_batch{1};
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

/**
 * Shared engine surface for the future WASM build and the native CLI.
 * This starts as a small skeleton so the migration can grow around a stable,
 * documented API instead of a one-off translation from TypeScript classes.
 */
class Engine {
 public:
  Engine();
  ~Engine();

  [[nodiscard]] const EngineCapabilities& capabilities() const noexcept;
  [[nodiscard]] EngineStatus status() const noexcept;
  [[nodiscard]] const EngineConfig& config() const noexcept;
  [[nodiscard]] const ImageBuffer& image() const noexcept;
  [[nodiscard]] const TargetStats& target_stats() const noexcept;
  [[nodiscard]] bool has_image() const noexcept;
  [[nodiscard]] bool has_optimizer() const noexcept;

  void configure(const EngineConfig& config);
  void load_image(ImageBuffer image);
  [[nodiscard]] ImageBuffer prepare_target(
      const ImageBuffer& source_image,
      const TargetProcessingConfig& config);
  void initialize_optimizer();
  [[nodiscard]] OptimizerProgress evolve_batch();
  [[nodiscard]] const std::vector<Dot>& best_dots() const;
  [[nodiscard]] OptimizerProgress optimizer_progress() const;
  [[nodiscard]] OptimizerValidation validate_optimizer() const;
  [[nodiscard]] std::string export_best_svg(int scale = 1) const;
  [[nodiscard]] std::vector<std::uint8_t> export_best_png(int scale = 1) const;
  [[nodiscard]] std::vector<std::uint8_t> render_best_grayscale(
      int scale = 1) const;
  [[nodiscard]] QualityMetrics best_quality_metrics() const;

 [[nodiscard]] std::string status_string() const;

 private:
  struct PyramidLevel {
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
  mutable std::vector<Dot> projected_best_dots_{};
  EngineStatus status_{EngineStatus::booting};

  [[nodiscard]] std::vector<Dot> project_dots_to_image_space(
      const std::vector<Dot>& dots,
      int source_width,
      int source_height) const;
  void initialize_level_optimizer(const std::vector<Dot>& seed_dots);
  void maybe_promote_level();
};

[[nodiscard]] std::string to_string(EngineStatus status);

}  // namespace stippling
