# Fixtures

This directory stores tracked inputs used by tests, benchmarks, and parity checks.

Current regression fixture families:

- `regression/bands.ppm`: small color fixture that exercises RGB decoding
- `regression/cross.pgm`: sparse diagonal structure used for parity smoke tests
- `regression/detail-grid.ppm`: higher-detail color fixture with mixed texture and edges
- `regression/edge-rings.pgm`: edge-heavy fixture for outline preservation
- `regression/portrait-mask.pgm`: portrait-like grayscale silhouette for face-oriented workloads
- `regression/sparse-stars.pgm`: sparse fixture for low-density dot allocation checks

These fixtures are intentionally small so CI can run parity and benchmark checks quickly while still covering different image structures.
