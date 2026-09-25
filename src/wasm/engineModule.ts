import createStipplingEngineModule, {
  GeneratedStipplingEngineModule,
} from "./generated/stipplingEngine.js";
import {
  EngineExportFormat,
  EngineRunConfig,
  SerializedDot,
  SerializedImageBuffer,
  TargetProcessingConfig,
  TargetStats,
} from "../shared/engineProtocol";

// Typed wrapper over the engine's C ABI (see c_api.h): copies buffers in and
// out of WASM memory and turns -1 return codes into thrown Errors.

/** Must match the C `StipplingExportFormat` enum. */
const EXPORT_FORMAT_CODES: Record<EngineExportFormat, number> = {
  svg: 0,
  png: 1,
  "timelapse-svg": 2,
};

/** A native `StipplingDot` is three consecutive f64 values: x, y, radius. */
const DOT_FIELD_COUNT = 3;

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
  exportArtifact(
    format: EngineExportFormat,
    scale: number,
    frameDurationMs: number
  ): ArrayBuffer;
  hasImage(): boolean;
  dispose(): void;
}

export interface WasmEngineModule {
  createEngine(): WasmEngineInstance;
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
      this.module._stippling_engine_configure(
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
      this.module._stippling_engine_evolve_batch(this.enginePointer),
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

  // Reads the handle-owned dot buffer in place: no malloc, no intermediate copy.
  public getBestDots(): SerializedDot[] {
    this.assertSuccess(
      this.module._stippling_engine_capture_best_dots(this.enginePointer),
      "capture the best dots"
    );

    const count = this.module._stippling_engine_best_dots_count(this.enginePointer);
    if (count === 0) {
      return [];
    }

    const values = new Float64Array(
      this.module.HEAPU8.buffer,
      this.module._stippling_engine_best_dots_data(this.enginePointer),
      count * DOT_FIELD_COUNT
    );
    const dots: SerializedDot[] = new Array(count);
    for (let index = 0; index < count; index += 1) {
      const offset = index * DOT_FIELD_COUNT;
      dots[index] = {
        x: values[offset],
        y: values[offset + 1],
        radius: values[offset + 2],
      };
    }
    return dots;
  }

  public exportArtifact(
    format: EngineExportFormat,
    scale: number,
    frameDurationMs: number
  ): ArrayBuffer {
    this.assertSuccess(
      this.module._stippling_engine_export(
        this.enginePointer,
        EXPORT_FORMAT_CODES[format],
        scale,
        frameDurationMs
      ),
      `export ${format}`
    );

    const pointer = this.module._stippling_engine_export_data(this.enginePointer);
    const size = this.module._stippling_engine_export_size(this.enginePointer);
    return this.module.HEAPU8.slice(pointer, pointer + size).buffer;
  }

  public hasImage(): boolean {
    return this.imageLoaded;
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

      return {
        width,
        height,
        format: "rgba8",
        pixels: this.module.HEAPU8.slice(
          outputPointer,
          outputPointer + copiedByteLength
        ).buffer,
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

export async function loadEngineModule(): Promise<WasmEngineModule> {
  const module = await createStipplingEngineModule();
  return {
    createEngine: () => new NativeWasmEngineInstance(module),
  };
}
