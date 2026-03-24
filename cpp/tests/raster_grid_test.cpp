#include "stippling/engine/dot.hpp"
#include "stippling/engine/raster_grid.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  {
    stippling::RasterGrid incremental(32, 32);
    stippling::RasterGrid full(32, 32);

    const stippling::Dot previous{10.0, 10.0, 3.0};
    const stippling::Dot next{18.0, 12.0, 3.0};

    incremental.draw_dot(previous);
    incremental.apply_dot_delta(previous, next);

    full.draw_dot(next);

    assert(incremental.pixels() == full.pixels());
  }

  {
    stippling::RasterGrid overlapping(32, 32);
    stippling::RasterGrid full(32, 32);
    const stippling::Dot left{10.0, 10.0, 4.0};
    const stippling::Dot right{13.0, 10.0, 4.0};

    overlapping.draw_dot(left);
    overlapping.draw_dot(right);
    overlapping.erase_dot(left);

    full.draw_dot(right);

    assert(overlapping.pixels() == full.pixels());
  }

  {
    stippling::RasterGrid grid(16, 16);
    const stippling::Dot dot{8.0, 8.0, 3.0};
    grid.draw_dot(dot);

    const std::vector<std::uint8_t> white_target(16 * 16, 255);
    assert(grid.squared_error(white_target) > 0);
  }

  return 0;
}
