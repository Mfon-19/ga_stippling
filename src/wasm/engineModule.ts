import { EngineCapabilities } from "../shared/engineProtocol";

export interface WasmEngineModule {
  capabilities: EngineCapabilities;
  dispose(): void;
}

/**
 * Placeholder loader for the future Emscripten-generated module.
 * The worker already speaks the right protocol, so swapping the stub out for
 * the real C++ backend should not require a UI rewrite.
 */
export async function loadEngineModule(): Promise<WasmEngineModule> {
  return {
    capabilities: {
      backend: "stub",
      incrementalFitness: false,
      multiscale: false,
      benchmarkMode: false,
      exportSvg: false,
      exportPng: false,
    },
    dispose() {
      // The stub backend has no native resources to release yet.
    },
  };
}
