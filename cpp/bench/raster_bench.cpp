// Microbenchmark isolating the engine's central performance claim: incremental
// raster fitness vs. full re-rasterization, measured inside the same native
// binary so the result reflects the algorithm rather than a C++ vs. JavaScript
// language gap.
//
// The unit of work is "evaluate the fitness impact of one proposed single-dot
// change," which is exactly what the genetic algorithm does thousands of times
// per generation:
//
//   * Full redraw (the pre-incremental design): with no persistent raster
//     state, testing any change forces re-rendering the whole candidate and
//     recomparing every pixel -> O(dots * footprint + pixels). Implemented as
//     clear() + draw all dots + squared_error().
//   * Incremental (this engine): apply_dot_delta_and_update_error() only touches
//     the pixels under the two dot footprints involved -> O(footprint).
//
// Before timing, every cell verifies the incremental error is bit-for-bit equal
// to a from-scratch full recompute, so the speedup can never come from the
// incremental path quietly doing less correct work.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "stippling/engine/dot.hpp"
#include "stippling/engine/raster_grid.hpp"

using stippling::Dot;
using stippling::RasterGrid;

namespace {

/** Matches the optimizer's supported stipple radius range. */
double clamp_radius(double value) { return std::clamp(value, 0.35, 1.35); }

/** Counts the pixels a filled circle of the given integer radius covers. */
int filled_circle_pixels(int radius) {
  if (radius <= 0) {
    return 1;
  }
  int total = 0;
  for (int y = -radius; y <= radius; ++y) {
    const auto span =
        static_cast<int>(std::floor(std::sqrt(static_cast<double>(radius) * radius -
                                              static_cast<double>(y) * y)));
    total += 2 * span + 1;
  }
  return total;
}

/** Builds a deterministic grayscale target with smooth structure plus texture. */
std::vector<std::uint8_t> make_target(int width, int height, std::uint64_t seed) {
  std::mt19937_64 random(seed);
  std::uniform_int_distribution<int> noise(-12, 12);
  std::vector<std::uint8_t> target(static_cast<std::size_t>(width) * height);
  const double center_x = width * 0.5;
  const double center_y = height * 0.5;
  const double max_distance = std::sqrt(center_x * center_x + center_y * center_y);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double dx = x - center_x;
      const double dy = y - center_y;
      const double distance = std::sqrt(dx * dx + dy * dy) / max_distance;
      const int base = static_cast<int>(255.0 * distance);
      const int value = std::clamp(base + noise(random), 0, 255);
      target[static_cast<std::size_t>(y) * width + x] =
          static_cast<std::uint8_t>(value);
    }
  }
  return target;
}

/** Generates a population of dots scattered across the image. */
std::vector<Dot> make_dots(int count, int width, int height, std::uint64_t seed) {
  std::mt19937_64 random(seed);
  std::uniform_real_distribution<double> x_distribution(0.0, width - 1.0);
  std::uniform_real_distribution<double> y_distribution(0.0, height - 1.0);
  std::uniform_real_distribution<double> radius_distribution(0.35, 1.35);

  std::vector<Dot> dots;
  dots.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    dots.push_back(Dot{x_distribution(random), y_distribution(random),
                       radius_distribution(random)});
  }
  return dots;
}

/** Produces a nearby alternate dot, mimicking a local-search mutation proposal. */
Dot perturb(const Dot& dot, int width, int height, std::mt19937_64& random) {
  std::uniform_real_distribution<double> shift(-2.0, 2.0);
  std::uniform_real_distribution<double> radius_shift(-0.4, 0.4);
  double x = std::clamp(dot.x + shift(random), 0.0, width - 1.0);
  double y = std::clamp(dot.y + shift(random), 0.0, height - 1.0);
  // Guarantee the proposal differs so the swap always does real work.
  if (std::floor(x) == std::floor(dot.x) && std::floor(y) == std::floor(dot.y)) {
    x = std::clamp(x + 1.0, 0.0, width - 1.0);
  }
  return Dot{x, y, clamp_radius(dot.radius + radius_shift(random))};
}

/** Repeats an operation until a minimum wall-clock budget elapses; returns ns/op. */
template <class Operation>
double time_ns_per_op(Operation&& operation, double min_seconds, long long min_iters) {
  using clock = std::chrono::steady_clock;
  long long iterations = 0;
  const auto started = clock::now();
  double elapsed = 0.0;
  do {
    operation();
    ++iterations;
    elapsed = std::chrono::duration<double>(clock::now() - started).count();
  } while (elapsed < min_seconds || iterations < min_iters);
  return (elapsed / static_cast<double>(iterations)) * 1e9;
}

struct BenchRow {
  std::string sweep;
  int width = 0;
  int height = 0;
  long long pixels = 0;
  int dots = 0;
  double avg_footprint_px = 0.0;
  double full_ns_per_eval = 0.0;
  double incremental_ns_per_eval = 0.0;
  double speedup = 0.0;
  bool agrees = false;
};

/** Renders every dot into a fresh raster, the cost paid without incremental state. */
void full_redraw(RasterGrid& grid, const std::vector<Dot>& dots,
                 const std::vector<std::uint8_t>& target, std::uint64_t& sink) {
  grid.clear();
  for (const auto& dot : dots) {
    grid.draw_dot(dot);
  }
  sink ^= grid.squared_error(target);
}

/**
 * Runs one (image size, dot count) cell: proves incremental == full, then times
 * both fitness-evaluation strategies.
 */
BenchRow run_cell(const std::string& sweep, int width, int height, int dot_count) {
  const std::uint64_t seed = 0x9E3779B97F4A7C15ull ^
                             (static_cast<std::uint64_t>(width) << 32) ^
                             (static_cast<std::uint64_t>(height) << 16) ^
                             static_cast<std::uint64_t>(dot_count);
  const auto target = make_target(width, height, seed);
  const auto dots = make_dots(dot_count, width, height, seed * 2654435761u + 1);

  BenchRow row;
  row.sweep = sweep;
  row.width = width;
  row.height = height;
  row.pixels = static_cast<long long>(width) * height;
  row.dots = dot_count;

  double footprint_total = 0.0;
  for (const auto& dot : dots) {
    footprint_total += filled_circle_pixels(static_cast<int>(std::floor(dot.radius)));
  }
  row.avg_footprint_px = dots.empty() ? 0.0 : footprint_total / dots.size();

  // Persistent raster holding the full population, used by the incremental path.
  RasterGrid incremental_grid(width, height);
  for (const auto& dot : dots) {
    incremental_grid.draw_dot(dot);
  }
  std::uint64_t base_error = incremental_grid.squared_error(target);

  // Correctness gate: a sample of proposals must update error exactly like a
  // from-scratch recompute, and reverting must restore the original error.
  std::mt19937_64 proposal_random(seed ^ 0xD1B54A32D192ED03ull);
  std::uniform_int_distribution<int> slot(0, dot_count - 1);
  const int sample_count = std::min(dot_count, 64);
  bool agrees = true;
  std::vector<Dot> mutable_dots = dots;
  RasterGrid reference_grid(width, height);

  for (int sample = 0; sample < sample_count && agrees; ++sample) {
    const int index = slot(proposal_random);
    const Dot original = dots[index];
    const Dot proposal = perturb(original, width, height, proposal_random);

    const std::uint64_t incremental_error =
        incremental_grid.apply_dot_delta_and_update_error(original, proposal, target,
                                                          base_error);

    mutable_dots[index] = proposal;
    reference_grid.clear();
    for (const auto& dot : mutable_dots) {
      reference_grid.draw_dot(dot);
    }
    const std::uint64_t reference_error = reference_grid.squared_error(target);
    mutable_dots[index] = original;

    const std::uint64_t restored_error =
        incremental_grid.apply_dot_delta_and_update_error(proposal, original, target,
                                                          incremental_error);

    if (incremental_error != reference_error || restored_error != base_error) {
      agrees = false;
    }
  }
  row.agrees = agrees;
  if (!agrees) {
    return row;
  }

  // Incremental timing: rotate through every slot, toggling each between its
  // original dot and a fixed proposal. Cycling all slots averages over the real
  // footprint distribution (instead of one lucky dot), and tracking the live
  // state per slot keeps the raster and running error exact with no drift. So
  // ns/op is the true average cost of evaluating one proposed dot change.
  std::vector<Dot> proposals(dots.size());
  std::vector<Dot> current = dots;
  for (std::size_t index = 0; index < dots.size(); ++index) {
    proposals[index] = perturb(dots[index], width, height, proposal_random);
  }
  std::uint64_t running_error = base_error;
  std::size_t cursor = 0;
  row.incremental_ns_per_eval = time_ns_per_op(
      [&]() {
        const Dot& live = current[cursor];
        const bool at_original =
            live.x == dots[cursor].x && live.y == dots[cursor].y &&
            live.radius == dots[cursor].radius;
        const Dot& next = at_original ? proposals[cursor] : dots[cursor];
        running_error = incremental_grid.apply_dot_delta_and_update_error(
            live, next, target, running_error);
        current[cursor] = next;
        cursor = (cursor + 1) % current.size();
      },
      0.30, 16);

  // Full-redraw timing: render the whole candidate and recompare every pixel.
  RasterGrid full_grid(width, height);
  std::uint64_t sink = 0;
  row.full_ns_per_eval = time_ns_per_op(
      [&]() { full_redraw(full_grid, dots, target, sink); }, 0.25, 4);
  if (sink == 0xFFFFFFFFFFFFFFFFull) {
    std::cerr << "";  // Prevent the optimizer from discarding the redraw work.
  }

  row.speedup = row.incremental_ns_per_eval > 0.0
                    ? row.full_ns_per_eval / row.incremental_ns_per_eval
                    : 0.0;
  return row;
}

void print_table(const std::vector<BenchRow>& rows) {
  std::cout << "\n";
  std::cout << std::left << std::setw(10) << "sweep" << std::right << std::setw(12)
            << "image" << std::setw(11) << "pixels" << std::setw(8) << "dots"
            << std::setw(11) << "footprint" << std::setw(15) << "full ns/eval"
            << std::setw(15) << "incr ns/eval" << std::setw(11) << "speedup"
            << std::setw(8) << "exact" << "\n";
  std::cout << std::string(101, '-') << "\n";
  for (const auto& row : rows) {
    std::ostringstream image;
    image << row.width << "x" << row.height;
    std::cout << std::left << std::setw(10) << row.sweep << std::right
              << std::setw(12) << image.str() << std::setw(11) << row.pixels
              << std::setw(8) << row.dots << std::setw(11) << std::fixed
              << std::setprecision(1) << row.avg_footprint_px << std::setw(15)
              << std::setprecision(1) << row.full_ns_per_eval << std::setw(15)
              << row.incremental_ns_per_eval << std::setw(10)
              << std::setprecision(1) << row.speedup << "x" << std::setw(8)
              << (row.agrees ? "yes" : "NO") << "\n";
  }
  std::cout << "\n";
}

std::string rows_to_csv(const std::vector<BenchRow>& rows) {
  std::ostringstream csv;
  csv << "sweep,width,height,pixels,dots,avg_footprint_px,full_ns_per_eval,"
         "incremental_ns_per_eval,speedup,exact_match\n";
  csv << std::fixed << std::setprecision(3);
  for (const auto& row : rows) {
    csv << row.sweep << ',' << row.width << ',' << row.height << ',' << row.pixels
        << ',' << row.dots << ',' << row.avg_footprint_px << ','
        << row.full_ns_per_eval << ',' << row.incremental_ns_per_eval << ','
        << row.speedup << ',' << (row.agrees ? 1 : 0) << '\n';
  }
  return csv.str();
}

std::string rows_to_json(const std::vector<BenchRow>& rows) {
  std::ostringstream json;
  json << std::fixed << std::setprecision(3);
  json << "{\n  \"unit\": \"nanoseconds per fitness evaluation of one proposed "
          "single-dot change\",\n  \"rows\": [\n";
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto& row = rows[index];
    json << "    {\"sweep\": \"" << row.sweep << "\", \"width\": " << row.width
         << ", \"height\": " << row.height << ", \"pixels\": " << row.pixels
         << ", \"dots\": " << row.dots
         << ", \"avgFootprintPx\": " << row.avg_footprint_px
         << ", \"fullNsPerEval\": " << row.full_ns_per_eval
         << ", \"incrementalNsPerEval\": " << row.incremental_ns_per_eval
         << ", \"speedup\": " << row.speedup
         << ", \"exactMatch\": " << (row.agrees ? "true" : "false") << "}";
    json << (index + 1 < rows.size() ? ",\n" : "\n");
  }
  json << "  ]\n}\n";
  return json.str();
}

void write_file(const std::string& path, const std::string& contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "Failed to write " << path << "\n";
    return;
  }
  out << contents;
}

}  // namespace

int main(int argc, char** argv) {
  std::string csv_path;
  std::string json_path;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--csv" && index + 1 < argc) {
      csv_path = argv[++index];
    } else if (argument == "--json" && index + 1 < argc) {
      json_path = argv[++index];
    } else {
      std::cerr << "Usage: stippling_raster_bench [--csv path] [--json path]\n";
      return 2;
    }
  }

  std::vector<BenchRow> rows;

  // Dot-count sweep at a fixed image size: isolates O(dots) growth of full
  // redraw while the incremental cost stays flat.
  const int fixed_width = 512;
  const int fixed_height = 512;
  for (const int dot_count : {250, 500, 1000, 2000, 4000, 8000}) {
    rows.push_back(run_cell("dots", fixed_width, fixed_height, dot_count));
  }

  // Image-size sweep at a fixed dot count: isolates O(pixels) growth of full
  // redraw's per-evaluation comparison.
  const int fixed_dots = 3000;
  for (const int side : {128, 256, 512, 1024}) {
    rows.push_back(run_cell("image", side, side, fixed_dots));
  }

  print_table(rows);

  bool all_exact = true;
  for (const auto& row : rows) {
    if (!row.agrees) {
      all_exact = false;
      std::cerr << "MISMATCH: incremental error diverged from full recompute at "
                << row.width << "x" << row.height << ", " << row.dots << " dots\n";
    }
  }

  const std::string csv = rows_to_csv(rows);
  std::cout << "CSV:\n" << csv;
  if (!csv_path.empty()) {
    write_file(csv_path, csv);
  }
  if (!json_path.empty()) {
    write_file(json_path, rows_to_json(rows));
  }

  if (!all_exact) {
    std::cerr << "\nIncremental bookkeeping did not match the reference; "
                 "benchmark numbers are not trustworthy.\n";
    return 1;
  }
  return 0;
}
