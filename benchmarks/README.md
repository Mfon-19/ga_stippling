# Benchmarks

This directory stores benchmark documentation, fixed benchmark configs, and local benchmark outputs.

Run the current headless image benchmark with:

```bash
npm run benchmark
```

Run the tracked-fixture smoke benchmark with:

```bash
npm run benchmark:fixtures
```

Compare the two newest benchmark reports with:

```bash
npm run benchmark:compare
```

Run the native CLI vs WASM parity check with:

```bash
npm run parity:check
```

The benchmark runner:

- decodes the root-level `jobs.jpeg` and `landscape.avif` fixtures
- can also run directly on tracked Netpbm fixtures under `fixtures/regression/`
- preprocesses them with the current default blur and threshold
- benchmarks both the TypeScript engine and the native WASM engine
- records `generations/sec`
- measures time to a shared quality target using a fixed seed
- records image-quality metrics such as MSE, RMSE, PSNR, and exact pixel ratio
- writes a machine-readable JSON report to `benchmarks/results/`

The parity check runs the same tracked fixtures through the native CLI and the browser/WASM engine, then compares best-dot snapshots, SVG exports, PNG exports, and validation results.

The generated JSON reports are intentionally gitignored because they are local measurement artifacts, not source files.
