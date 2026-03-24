# Archived TypeScript GA

This directory contains the original TypeScript implementation of the
stippling genetic algorithm.

It has been archived for historical reference and optional baseline benchmarking.
It is no longer part of the active browser runtime path.

The live application now uses:

- browser UI in `src/`
- worker orchestration in `src/worker/`
- the native C++/WASM engine for evolution

These archived files are useful when:

- comparing current performance against the original implementation
- reviewing how the project evolved architecturally
- understanding the simpler prototype before the C++/WASM migration
