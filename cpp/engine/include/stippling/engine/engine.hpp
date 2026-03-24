#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stippling/engine/dot.hpp"

namespace stippling {

class Optimizer;

struct OptimizerProgress {
  std::uint32_t generation{0};
  double best_fitness{0.0};
  std::uint64_t best_squared_error{0};
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
  double elitism_ratio{0.7};
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

  [[nodiscard]] std::string status_string() const;

 private:
  EngineCapabilities capabilities_{};
  EngineConfig config_{};
  ImageBuffer image_{};
  TargetStats target_stats_{};
  std::unique_ptr<Optimizer> optimizer_{};
  EngineStatus status_{EngineStatus::booting};
};

[[nodiscard]] std::string to_string(EngineStatus status);

}  // namespace stippling
