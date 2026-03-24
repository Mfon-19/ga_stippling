#include "stippling/engine/engine.hpp"

#include <stdexcept>
#include <utility>

namespace stippling {

bool ImageBuffer::valid() const noexcept {
  if (width <= 0 || height <= 0) {
    return false;
  }

  const auto expected_size =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  return pixels.size() == expected_size;
}

Engine::Engine() {
  status_ = EngineStatus::idle;
}

const EngineCapabilities& Engine::capabilities() const noexcept {
  return capabilities_;
}

EngineStatus Engine::status() const noexcept {
  return status_;
}

const EngineConfig& Engine::config() const noexcept {
  return config_;
}

const ImageBuffer& Engine::image() const noexcept {
  return image_;
}

bool Engine::has_image() const noexcept {
  return image_.valid();
}

void Engine::configure(const EngineConfig& config) {
  config_ = config;
  status_ = has_image() ? EngineStatus::image_loaded : EngineStatus::configured;
}

void Engine::load_image(ImageBuffer image) {
  // The native core will operate on tightly packed grayscale/importance buffers.
  // Validating that assumption early keeps the browser and CLI integrations honest.
  if (!image.valid()) {
    throw std::invalid_argument(
        "ImageBuffer must contain width * height grayscale pixels");
  }

  image_ = std::move(image);
  status_ = EngineStatus::image_loaded;
}

std::string Engine::status_string() const {
  return to_string(status_);
}

std::string to_string(EngineStatus status) {
  switch (status) {
    case EngineStatus::booting:
      return "booting";
    case EngineStatus::idle:
      return "idle";
    case EngineStatus::configured:
      return "configured";
    case EngineStatus::image_loaded:
      return "image_loaded";
  }

  throw std::invalid_argument("Unknown EngineStatus");
}

}  // namespace stippling
