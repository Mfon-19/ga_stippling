# Benchmarks

This directory stores benchmark documentation, fixed benchmark configs, and local benchmark outputs.

Run the current headless image benchmark with:

```bash
npm run benchmark
```

The benchmark runner:

- decodes the root-level `jobs.jpeg` and `landscape.avif` fixtures
- preprocesses them with the current default blur and threshold
- benchmarks both the TypeScript engine and the native WASM engine
- records `generations/sec`
- measures time to a shared quality target using a fixed seed
- writes a machine-readable JSON report to `benchmarks/results/`

The generated JSON reports are intentionally gitignored because they are local measurement artifacts, not source files.
