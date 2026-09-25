import { defineConfig } from 'vite';

// Cross-origin isolation enables SharedArrayBuffer, which the threaded engine
// build needs. Hosts without these headers get the single-threaded build.
const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
};

export default defineConfig({
  root: '.',
  base: './',
  server: { headers: isolationHeaders },
  preview: { headers: isolationHeaders },
  // ES workers so the engine worker can load the WASM module on demand and the
  // threaded build can start its own module workers. The threaded module's
  // Node-only branch (used by the parity check) never runs in the browser.
  worker: {
    format: 'es',
    rollupOptions: { external: ['node:module', 'node:worker_threads'] },
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    // The threaded module uses top-level await. The engine already needs WASM
    // exception support (Chrome 95, Firefox 100, Safari 15.2), all of which
    // run ES2022.
    target: 'es2022',
  }
});