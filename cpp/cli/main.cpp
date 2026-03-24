#include "stippling/engine/engine.hpp"
#include "stippling/engine/export.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct RunOptions {
  std::uint32_t generations{10};
  std::uint32_t population{100};
  double mutation{0.2};
  double elitism{0.15};
  std::optional<std::uint32_t> dot_count{};
  std::uint32_t seed{1337};
  std::uint32_t blur{0};
  std::uint32_t threshold{130};
  std::uint32_t max_dot_count{200000};
  std::uint32_t scale{4};
  std::uint32_t frame_stride{1};
  std::uint32_t frame_duration_ms{120};
  bool validate{false};
  bool include_dots{false};
};

struct RunArtifacts {
  std::optional<fs::path> svg_path{};
  std::optional<fs::path> png_path{};
  std::optional<fs::path> timelapse_path{};
  std::optional<fs::path> report_path{};
};

struct BatchArtifacts {
  bool emit_svg{false};
  bool emit_png{false};
  bool emit_timelapse{false};
  bool emit_report{false};
  std::optional<fs::path> summary_report_path{};
};

struct RunResult {
  fs::path input_path{};
  int width{0};
  int height{0};
  std::uint32_t generations_requested{0};
  double elapsed_ms{0.0};
  double generations_per_second{0.0};
  std::uint32_t dot_count{0};
  stippling::TargetStats target_stats{};
  stippling::OptimizerProgress progress{};
  stippling::OptimizerValidation validation{};
  bool validation_requested{false};
  stippling::QualityMetrics quality{};
  RunOptions options{};
  std::vector<stippling::Dot> best_dots{};
  std::size_t timelapse_frames{0};
  RunArtifacts artifacts{};
};

void print_usage() {
  std::cout << "Usage:\n"
            << "  stippling_cli run --input <image.pgm|ppm> [options]\n"
            << "  stippling_cli batch --input-dir <dir> --output-dir <dir> [options]\n"
            << "  stippling_cli benchmark --input <image.pgm|ppm> --report <file|-> [options]\n"
            << "  stippling_cli benchmark --input-dir <dir> --report <file|-> [options]\n\n"
            << "Common options:\n"
            << "  --generations <n>\n"
            << "  --population <n>\n"
            << "  --mutation <float>\n"
            << "  --elitism <float>\n"
            << "  --dot-count <n>\n"
            << "  --seed <n>\n"
            << "  --blur <n>\n"
            << "  --threshold <n>\n"
            << "  --scale <n>\n"
            << "  --validate\n"
            << "  --include-dots\n\n"
            << "Run-specific outputs:\n"
            << "  --svg <file>\n"
            << "  --png <file>\n"
            << "  --timelapse <file>\n"
            << "  --report <file|->\n\n"
            << "Batch outputs:\n"
            << "  --emit-svg\n"
            << "  --emit-png\n"
            << "  --emit-timelapse\n"
            << "  --emit-report\n"
            << "  --report <file|->\n";
}

std::string json_escape(std::string_view value) {
  std::ostringstream stream;
  for (const auto character : value) {
    switch (character) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        stream << character;
        break;
    }
  }
  return stream.str();
}

std::string next_token(std::istream& stream) {
  std::string token;

  while (stream >> token) {
    if (!token.empty() && token[0] == '#') {
      std::string comment;
      std::getline(stream, comment);
      continue;
    }
    return token;
  }

  throw std::runtime_error("Unexpected end of image header");
}

stippling::ImageBuffer load_netpbm_image(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open input image: " + path.string());
  }

  const auto magic = next_token(input);
  const auto width = std::stoi(next_token(input));
  const auto height = std::stoi(next_token(input));
  const auto max_value = std::stoi(next_token(input));
  if (max_value != 255) {
    throw std::runtime_error("Only 8-bit Netpbm images are supported");
  }

  const auto pixel_count = static_cast<std::size_t>(width * height);
  const auto convert_grayscale_to_rgba = [&](const std::vector<std::uint8_t>& grayscale) {
    std::vector<std::uint8_t> rgba(pixel_count * 4u, 255u);
    for (std::size_t index = 0; index < grayscale.size(); ++index) {
      rgba[index * 4u] = grayscale[index];
      rgba[index * 4u + 1u] = grayscale[index];
      rgba[index * 4u + 2u] = grayscale[index];
    }
    return rgba;
  };

  if (magic == "P2") {
    std::vector<std::uint8_t> grayscale;
    grayscale.reserve(pixel_count);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      grayscale.push_back(static_cast<std::uint8_t>(std::stoi(next_token(input))));
    }
    return {
        .format = stippling::PixelFormat::rgba8,
        .width = width,
        .height = height,
        .pixels = convert_grayscale_to_rgba(grayscale),
    };
  }

  if (magic == "P3") {
    std::vector<std::uint8_t> rgba(pixel_count * 4u, 255u);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      rgba[index * 4u] =
          static_cast<std::uint8_t>(std::stoi(next_token(input)));
      rgba[index * 4u + 1u] =
          static_cast<std::uint8_t>(std::stoi(next_token(input)));
      rgba[index * 4u + 2u] =
          static_cast<std::uint8_t>(std::stoi(next_token(input)));
    }
    return {
        .format = stippling::PixelFormat::rgba8,
        .width = width,
        .height = height,
        .pixels = std::move(rgba),
    };
  }

  input.get();

  if (magic == "P5") {
    std::vector<std::uint8_t> grayscale(pixel_count);
    input.read(reinterpret_cast<char*>(grayscale.data()),
               static_cast<std::streamsize>(grayscale.size()));
    return {
        .format = stippling::PixelFormat::rgba8,
        .width = width,
        .height = height,
        .pixels = convert_grayscale_to_rgba(grayscale),
    };
  }

  if (magic == "P6") {
    std::vector<std::uint8_t> rgb(pixel_count * 3u);
    input.read(reinterpret_cast<char*>(rgb.data()),
               static_cast<std::streamsize>(rgb.size()));
    std::vector<std::uint8_t> rgba(pixel_count * 4u, 255u);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      rgba[index * 4u] = rgb[index * 3u];
      rgba[index * 4u + 1u] = rgb[index * 3u + 1u];
      rgba[index * 4u + 2u] = rgb[index * 3u + 2u];
    }
    return {
        .format = stippling::PixelFormat::rgba8,
        .width = width,
        .height = height,
        .pixels = std::move(rgba),
    };
  }

  throw std::runtime_error("Unsupported Netpbm format: " + magic);
}

void write_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Failed to open output file: " + path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void write_text(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Failed to open output file: " + path.string());
  }
  output << text;
}

void write_optional_text(const std::optional<fs::path>& path, const std::string& text) {
  if (!path) {
    return;
  }
  if (*path == fs::path("-")) {
    std::cout << text << '\n';
    return;
  }
  write_text(*path, text);
}

std::string dots_to_json(const std::vector<stippling::Dot>& dots) {
  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < dots.size(); ++index) {
    if (index > 0) {
      stream << ',';
    }
    stream << "{\"x\":" << dots[index].x << ",\"y\":" << dots[index].y
           << ",\"radius\":" << dots[index].radius << '}';
  }
  stream << ']';
  return stream.str();
}

std::string report_to_json(const RunResult& result) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(6);
  stream << "{";
  stream << "\"input\":\"" << json_escape(result.input_path.string()) << "\",";
  stream << "\"image\":{\"width\":" << result.width << ",\"height\":" << result.height
         << "},";
  stream << "\"config\":{"
         << "\"population\":" << result.options.population << ','
         << "\"mutation\":" << result.options.mutation << ','
         << "\"elitism\":" << result.options.elitism << ','
         << "\"dotCount\":" << result.dot_count << ','
         << "\"seed\":" << result.options.seed << ','
         << "\"blur\":" << result.options.blur << ','
         << "\"threshold\":" << result.options.threshold << ','
         << "\"scale\":" << result.options.scale << "},";
  stream << "\"target\":{"
         << "\"blackPixels\":" << result.target_stats.black_pixels << ','
         << "\"totalPixels\":" << result.target_stats.total_pixels << ','
         << "\"blackPercentage\":" << result.target_stats.black_percentage << ','
         << "\"recommendedDotCount\":" << result.target_stats.recommended_dot_count
         << "},";
  stream << "\"progress\":{"
         << "\"generation\":" << result.progress.generation << ','
         << "\"bestFitness\":" << result.progress.best_fitness << ','
         << "\"bestSquaredError\":" << result.progress.best_squared_error << ','
         << "\"elapsedMs\":" << result.elapsed_ms << ','
         << "\"generationsPerSecond\":" << result.generations_per_second << "},";
  stream << "\"quality\":{"
         << "\"mse\":" << result.quality.mse << ','
         << "\"rmse\":" << result.quality.rmse << ','
         << "\"psnr\":" << result.quality.psnr << ','
         << "\"exactPixelRatio\":" << result.quality.exact_pixel_ratio << "},";
  stream << "\"timelapse\":{"
         << "\"frames\":" << result.timelapse_frames << ','
         << "\"frameStride\":" << result.options.frame_stride << ','
         << "\"frameDurationMs\":" << result.options.frame_duration_ms << "},";
  stream << "\"validation\":{"
         << "\"requested\":" << (result.validation_requested ? "true" : "false") << ','
         << "\"valid\":" << (result.validation.valid ? "true" : "false") << ','
         << "\"checkedCandidates\":" << result.validation.checked_candidates << ','
         << "\"mismatchedCandidates\":" << result.validation.mismatched_candidates
         << ','
         << "\"firstMismatchIndex\":" << result.validation.first_mismatch_index << ','
         << "\"maxSquaredErrorDelta\":" << result.validation.max_squared_error_delta
         << ','
         << "\"totalPixelMismatches\":" << result.validation.total_pixel_mismatches
         << "},";
  stream << "\"artifacts\":{"
         << "\"svg\":" << (result.artifacts.svg_path
                                ? ('"' + json_escape(result.artifacts.svg_path->string()) + '"')
                                : "null")
         << ','
         << "\"png\":" << (result.artifacts.png_path
                                ? ('"' + json_escape(result.artifacts.png_path->string()) + '"')
                                : "null")
         << ','
         << "\"timelapse\":" << (result.artifacts.timelapse_path
                                      ? ('"' + json_escape(result.artifacts.timelapse_path->string()) + '"')
                                      : "null")
         << "}";
  if (result.options.include_dots) {
    stream << ",\"bestDots\":" << dots_to_json(result.best_dots);
  }
  stream << "}";
  return stream.str();
}

RunResult execute_run(const fs::path& input_path,
                      const RunOptions& options,
                      const RunArtifacts& artifacts) {
  auto source_image = load_netpbm_image(input_path);
  stippling::Engine engine;
  const auto prepared = engine.prepare_target(
      source_image,
      {
          .blur_amount = options.blur,
          .threshold = options.threshold,
          .max_dot_count = options.max_dot_count,
      });

  const auto resolved_dot_count =
      options.dot_count.value_or(engine.target_stats().recommended_dot_count);
  if (resolved_dot_count == 0) {
    throw std::runtime_error("Dot count resolved to zero for input " + input_path.string());
  }

  engine.configure({
      .population_size = options.population,
      .mutation_rate = options.mutation,
      .dot_count = resolved_dot_count,
      .elitism_ratio = options.elitism,
      .seed = options.seed,
      .generations_per_batch = 1,
  });
  engine.initialize_optimizer();

  std::vector<stippling::TimelapseFrame> frames;
  frames.push_back({.generation = 0, .dots = engine.best_dots()});

  const auto started_at = std::chrono::steady_clock::now();
  stippling::OptimizerProgress progress = engine.optimizer_progress();
  for (std::uint32_t generation = 0; generation < options.generations; ++generation) {
    progress = engine.evolve_batch();
    if (options.frame_stride > 0 &&
        ((generation + 1) % options.frame_stride == 0 ||
         generation + 1 == options.generations)) {
      frames.push_back({.generation = progress.generation, .dots = engine.best_dots()});
    }
  }
  const auto elapsed_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started_at)
                              .count();

  if (artifacts.svg_path) {
    write_text(*artifacts.svg_path, engine.export_best_svg(static_cast<int>(options.scale)));
  }
  if (artifacts.png_path) {
    write_bytes(*artifacts.png_path,
                engine.export_best_png(static_cast<int>(options.scale)));
  }
  if (artifacts.timelapse_path) {
    write_text(*artifacts.timelapse_path,
               stippling::export_timelapse_to_svg(
                   frames,
                   prepared.width,
                   prepared.height,
                   static_cast<int>(options.scale),
                   options.frame_duration_ms));
  }

  return {
      .input_path = input_path,
      .width = prepared.width,
      .height = prepared.height,
      .generations_requested = options.generations,
      .elapsed_ms = elapsed_ms,
      .generations_per_second =
          elapsed_ms > 0.0 ? static_cast<double>(options.generations) / (elapsed_ms / 1000.0)
                           : 0.0,
      .dot_count = resolved_dot_count,
      .target_stats = engine.target_stats(),
      .progress = progress,
      .validation = options.validate ? engine.validate_optimizer()
                                     : stippling::OptimizerValidation{},
      .validation_requested = options.validate,
      .quality = engine.best_quality_metrics(),
      .options = options,
      .best_dots = options.include_dots ? engine.best_dots() : std::vector<stippling::Dot>{},
      .timelapse_frames = frames.size(),
      .artifacts = artifacts,
  };
}

std::vector<fs::path> collect_input_files(const fs::path& input_dir) {
  std::vector<fs::path> inputs;

  for (const auto& entry : fs::directory_iterator(input_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const auto extension = entry.path().extension().string();
    if (extension == ".pgm" || extension == ".ppm" || extension == ".pnm") {
      inputs.push_back(entry.path());
    }
  }

  std::sort(inputs.begin(), inputs.end());
  return inputs;
}

std::string batch_results_to_json(const std::vector<RunResult>& results) {
  std::ostringstream stream;
  stream << "{\"runs\":[";
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (index > 0) {
      stream << ',';
    }
    stream << report_to_json(results[index]);
  }
  stream << "]}";
  return stream.str();
}

std::string require_value(const std::vector<std::string>& args, std::size_t* index) {
  if (*index + 1 >= args.size()) {
    throw std::runtime_error("Missing value for " + args[*index]);
  }
  *index += 1;
  return args[*index];
}

RunOptions parse_run_options(const std::vector<std::string>& args,
                             std::size_t start_index,
                             std::optional<fs::path>* input_path,
                             RunArtifacts* artifacts,
                             std::optional<fs::path>* input_dir,
                             std::optional<fs::path>* output_dir,
                             BatchArtifacts* batch_artifacts) {
  RunOptions options;

  for (std::size_t index = start_index; index < args.size(); ++index) {
    const auto& argument = args[index];
    if (argument == "--input") {
      *input_path = require_value(args, &index);
    } else if (argument == "--input-dir") {
      *input_dir = require_value(args, &index);
    } else if (argument == "--output-dir") {
      *output_dir = require_value(args, &index);
    } else if (argument == "--generations") {
      options.generations = static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--population") {
      options.population = static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--mutation") {
      options.mutation = std::stod(require_value(args, &index));
    } else if (argument == "--elitism") {
      options.elitism = std::stod(require_value(args, &index));
    } else if (argument == "--dot-count") {
      options.dot_count =
          static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--seed") {
      options.seed = static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--blur") {
      options.blur = static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--threshold") {
      options.threshold = static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--scale") {
      options.scale = static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--frame-stride") {
      options.frame_stride =
          static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--frame-duration-ms") {
      options.frame_duration_ms =
          static_cast<std::uint32_t>(std::stoul(require_value(args, &index)));
    } else if (argument == "--validate") {
      options.validate = true;
    } else if (argument == "--include-dots") {
      options.include_dots = true;
    } else if (argument == "--svg") {
      artifacts->svg_path = require_value(args, &index);
    } else if (argument == "--png") {
      artifacts->png_path = require_value(args, &index);
    } else if (argument == "--timelapse") {
      artifacts->timelapse_path = require_value(args, &index);
    } else if (argument == "--report") {
      artifacts->report_path = require_value(args, &index);
      batch_artifacts->summary_report_path = artifacts->report_path;
    } else if (argument == "--emit-svg") {
      batch_artifacts->emit_svg = true;
    } else if (argument == "--emit-png") {
      batch_artifacts->emit_png = true;
    } else if (argument == "--emit-timelapse") {
      batch_artifacts->emit_timelapse = true;
    } else if (argument == "--emit-report") {
      batch_artifacts->emit_report = true;
    } else {
      throw std::runtime_error("Unknown argument: " + argument);
    }
  }

  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      print_usage();
      return 1;
    }

    const std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);
    RunArtifacts artifacts;
    BatchArtifacts batch_artifacts;
    std::optional<fs::path> input_path;
    std::optional<fs::path> input_dir;
    std::optional<fs::path> output_dir;
    const auto options = parse_run_options(args, 0, &input_path, &artifacts, &input_dir,
                                           &output_dir, &batch_artifacts);

    if (command == "run") {
      if (!input_path) {
        throw std::runtime_error("run requires --input");
      }

      const auto result = execute_run(*input_path, options, artifacts);
      const auto report = report_to_json(result);
      if (artifacts.report_path) {
        write_optional_text(artifacts.report_path, report);
      } else {
        std::cout << report << '\n';
      }
      return 0;
    }

    if (command == "batch" || command == "benchmark") {
      if (input_dir) {
        if (!output_dir && command == "batch") {
          throw std::runtime_error("batch requires --output-dir");
        }

        std::vector<RunResult> results;
        for (const auto& entry : collect_input_files(*input_dir)) {
          RunArtifacts per_file_artifacts{};
          if (output_dir) {
            const auto stem = entry.stem().string();
            if (batch_artifacts.emit_svg) {
              per_file_artifacts.svg_path = *output_dir / (stem + ".svg");
            }
            if (batch_artifacts.emit_png) {
              per_file_artifacts.png_path = *output_dir / (stem + ".png");
            }
            if (batch_artifacts.emit_timelapse) {
              per_file_artifacts.timelapse_path =
                  *output_dir / (stem + ".timelapse.svg");
            }
            if (batch_artifacts.emit_report) {
              per_file_artifacts.report_path = *output_dir / (stem + ".json");
            }
          }

          const auto result = execute_run(entry, options, per_file_artifacts);
          if (per_file_artifacts.report_path) {
            write_text(*per_file_artifacts.report_path, report_to_json(result));
          }
          results.push_back(result);
        }

        const auto summary = batch_results_to_json(results);
        if (batch_artifacts.summary_report_path) {
          write_optional_text(batch_artifacts.summary_report_path, summary);
        } else {
          std::cout << summary << '\n';
        }
        return 0;
      }

      if (!input_path) {
        throw std::runtime_error(command + " requires --input or --input-dir");
      }

      const auto result = execute_run(*input_path, options, artifacts);
      const auto report = report_to_json(result);
      if (artifacts.report_path) {
        write_optional_text(artifacts.report_path, report);
      } else {
        std::cout << report << '\n';
      }
      return 0;
    }

    print_usage();
    throw std::runtime_error("Unknown command: " + command);
  } catch (const std::exception& error) {
    std::cerr << "stippling_cli error: " << error.what() << '\n';
    return 1;
  }
}
