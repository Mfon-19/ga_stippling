import createStipplingEngineModule, {
  GeneratedStipplingEngineModule,
} from "./generated/stipplingEngine.js";
import {
  EngineCapabilities,
  EngineRunConfig,
  SerializedDot,
  SerializedImageBuffer,
  TargetProcessingConfig,
  TargetStats,
} from "../shared/engineProtocol";

const DOT_STRIDE_BYTES = 24;

interface PreparedTargetResult {
  image: SerializedImageBuffer;
  stats: TargetStats;
}

interface OptimizerBatchResult {
  generation: number;
  bestFitness: number;
}

export interface WasmEngineInstance {
  prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig
  ): PreparedTargetResult;
  configure(config: EngineRunConfig): void;
  initializeOptimizer(): void;
  evolveBatch(): OptimizerBatchResult;
  getBestDots(): SerializedDot[];
  hasImage(): boolean;
  heapByteLength(): number;
  dispose(): void;
}

export interface WasmEngineModule {
  capabilities: EngineCapabilities;
  createEngine(): WasmEngineInstance;
  dispose(): void;
}

class NativeWasmEngineInstance implements WasmEngineInstance {
  private enginePointer: number;
  private imageLoaded = false;

  constructor(private module: GeneratedStipplingEngineModule) {
    this.enginePointer = this.module._stippling_engine_create();
    if (!this.enginePointer) {
      throw new Error("Failed to create the native stippling engine");
    }
  }

  public prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig
  ): PreparedTargetResult {
    if (image.format !== "rgba8") {
      throw new Error(`Unsupported image format: ${image.format}`);
    }

    const sourcePixels = new Uint8Array(image.pixels);
    const sourcePointer = this.allocateBytes(sourcePixels.byteLength);

    try {
      this.module.HEAPU8.set(sourcePixels, sourcePointer);

      this.assertSuccess(
        this.module._stippling_engine_prepare_target_rgba8(
          this.enginePointer,
          image.width,
          image.height,
          sourcePointer,
          sourcePixels.byteLength,
          processing.blurAmount,
          processing.threshold,
          processing.maxDotCount
        ),
        "prepare the native target image"
      );

      this.imageLoaded = true;
      return {
        image: this.copyPreparedImage(),
        stats: {
          blackPixels: this.module._stippling_engine_target_black_pixels(
            this.enginePointer
          ),
          totalPixels: this.module._stippling_engine_target_total_pixels(
            this.enginePointer
          ),
          blackPercentage: this.module._stippling_engine_target_black_percentage(
            this.enginePointer
          ),
          recommendedDotCount:
            this.module._stippling_engine_target_recommended_dot_count(
              this.enginePointer
            ),
        },
      };
    } finally {
      this.module._free(sourcePointer);
    }
  }

  public configure(config: EngineRunConfig): void {
    this.assertSuccess(
      this.module._stippling_engine_configure_values(
        this.enginePointer,
        config.populationSize,
        config.mutationRate,
        config.dotCount,
        config.elitismRatio,
        config.seed >>> 0,
        config.generationsPerBatch
      ),
      "configure the native optimizer"
    );
  }

  public initializeOptimizer(): void {
    this.assertSuccess(
      this.module._stippling_engine_initialize_optimizer(this.enginePointer),
      "initialize the native optimizer"
    );
  }

  public evolveBatch(): OptimizerBatchResult {
    this.assertSuccess(
      this.module._stippling_engine_evolve_batch_in_place(this.enginePointer),
      "advance the native optimizer"
    );

    return {
      generation: this.module._stippling_engine_optimizer_generation(
        this.enginePointer
      ),
      bestFitness: this.module._stippling_engine_optimizer_best_fitness(
        this.enginePointer
      ),
    };
  }

  public getBestDots(): SerializedDot[] {
    const dotCount = this.module._stippling_engine_best_dot_count(
      this.enginePointer
    );
    if (dotCount === 0) {
      return [];
    }

    const dotsPointer = this.allocateBytes(dotCount * DOT_STRIDE_BYTES);

    try {
      const copiedCount = this.module._stippling_engine_copy_best_dots(
        this.enginePointer,
        dotsPointer,
        dotCount
      );
      const view = new DataView(
        this.module.HEAPU8.buffer,
        dotsPointer,
        copiedCount * DOT_STRIDE_BYTES
      );
      const dots: SerializedDot[] = [];

      for (let index = 0; index < copiedCount; index += 1) {
        const offset = index * DOT_STRIDE_BYTES;
        dots.push({
          x: view.getFloat64(offset, true),
          y: view.getFloat64(offset + 8, true),
          radius: view.getFloat64(offset + 16, true),
        });
      }

      return dots;
    } finally {
      this.module._free(dotsPointer);
    }
  }

  public hasImage(): boolean {
    return this.imageLoaded;
  }

  public heapByteLength(): number {
    return this.module.HEAPU8.byteLength;
  }

  public dispose(): void {
    if (this.enginePointer) {
      this.module._stippling_engine_destroy(this.enginePointer);
      this.enginePointer = 0;
    }
  }

  private copyPreparedImage(): SerializedImageBuffer {
    const width = this.module._stippling_engine_prepared_image_width(
      this.enginePointer
    );
    const height = this.module._stippling_engine_prepared_image_height(
      this.enginePointer
    );
    const byteLength = this.module._stippling_engine_prepared_image_byte_length(
      this.enginePointer
    );
    const outputPointer = this.allocateBytes(byteLength);

    try {
      const copiedByteLength = this.module._stippling_engine_copy_prepared_image_rgba8(
        this.enginePointer,
        outputPointer,
        byteLength
      );
      const pixels = new Uint8ClampedArray(copiedByteLength);
      pixels.set(
        this.module.HEAPU8.subarray(outputPointer, outputPointer + copiedByteLength)
      );

      return {
        width,
        height,
        format: "rgba8",
        pixels: pixels.buffer,
      };
    } finally {
      this.module._free(outputPointer);
    }
  }

  private allocateBytes(length: number): number {
    const pointer = this.module._malloc(Math.max(length, 1));
    if (!pointer) {
      throw new Error(`Failed to allocate ${length} bytes in WASM memory`);
    }
    return pointer;
  }

  private assertSuccess(statusCode: number, action: string): void {
    if (statusCode === 0) {
      return;
    }

    const errorPointer = this.module._stippling_engine_last_error(this.enginePointer);
    const nativeError =
      errorPointer !== 0 ? this.module.UTF8ToString(errorPointer) : "";
    const detail = nativeError ? `: ${nativeError}` : "";
    throw new Error(`Failed to ${action}${detail}`);
  }
}

class NativeWasmEngineModule implements WasmEngineModule {
  public readonly capabilities: EngineCapabilities = {
    backend: "wasm",
    incrementalFitness: true,
    multiscale: false,
    benchmarkMode: true,
    exportSvg: false,
    exportPng: false,
  };

  constructor(private module: GeneratedStipplingEngineModule) {}

  public createEngine(): WasmEngineInstance {
    return new NativeWasmEngineInstance(this.module);
  }

  public dispose(): void {
    // The generated Emscripten module does not expose a separate shutdown hook.
  }
}

export async function loadEngineModule(): Promise<WasmEngineModule> {
  const module = await createStipplingEngineModule();
  return new NativeWasmEngineModule(module);
}
