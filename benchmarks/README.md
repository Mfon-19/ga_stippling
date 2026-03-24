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

Or point the benchmark runner at a whole fixture directory:

```bash
./scripts/run-benchmark.sh fixtures/regression
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
- can also run directly on tracked Netpbm fixtures or whole directories under `fixtures/regression/`
- preprocesses them with the current default blur and threshold
- benchmarks both the TypeScript engine and the native WASM engine
- records `generations/sec`
- measures time to a shared quality target using a fixed seed
- records image-quality metrics such as MSE, RMSE, PSNR, and exact pixel ratio
- writes a machine-readable JSON report to `benchmarks/results/`

The parity check auto-discovers tracked fixtures under `fixtures/regression/` by default, and it can also be pointed at a file or directory explicitly. It runs those fixtures through the native CLI and the browser/WASM engine, then compares best-dot snapshots, SVG exports, PNG exports, and validation results.

The generated JSON reports are intentionally gitignored because they are local measurement artifacts, not source files.
