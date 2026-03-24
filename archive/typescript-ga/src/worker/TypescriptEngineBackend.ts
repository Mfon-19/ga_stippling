import { GeneticAlgorithm } from "../core/GeneticAlgorithm";
import { RasterImageProcessor } from "../../../../src/shared/RasterImageProcessor";
import { createSeededRandomSource } from "../../../../src/shared/random";
import {
  EngineArtifactEvent,
  EngineExportFormat,
  EngineExportOptions,
  EngineRunMetrics,
  EngineRunConfig,
  EngineSnapshotEvent,
  SerializedDot,
  SerializedImageBuffer,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../../../../src/shared/engineProtocol";
import {
  BackendCallbacks,
  WorkerEngineBackend,
} from "../../../../src/worker/WorkerEngineBackend";

/**
 * Transitional backend that runs the existing TypeScript optimizer inside the
 * worker. The public surface is shaped like the future native backend so the
 * UI can move off the main thread before the C++ port is finished.
 */
export class TypescriptEngineBackend implements WorkerEngineBackend {
  private rasterProcessor = new RasterImageProcessor();
  private imageData: ImageData | null = null;
  private geneticAlgorithm: GeneticAlgorithm | null = null;
  private runId: string | null = null;
  private generation = 0;
  private batchTimer: ReturnType<typeof setTimeout> | null = null;
  private lastSnapshotAt = 0;
  private currentConfig: EngineRunConfig | null = null;
  private startedAt = 0;
  private currentSeed = 0;

  public prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig,
    requestId: string
  ): TargetPreparedEvent {
    if (image.format !== "rgba8") {
      throw new Error(`Unsupported image format: ${image.format}`);
    }

    this.stop();
    const sourceImageData = new ImageData(
      new Uint8ClampedArray(image.pixels),
      image.width,
      image.height
    );
    const result = this.rasterProcessor.preprocess(sourceImageData, processing);
    this.imageData = result.imageData;
    const serializedPixels = new Uint8ClampedArray(result.imageData.data);

    return {
      type: "target-prepared",
      requestId,
      status: "loaded",
      image: {
        width: result.imageData.width,
        height: result.imageData.height,
        format: "rgba8",
        pixels: serializedPixels.buffer,
      },
      stats: result.stats,
    };
  }

  public startRun(
    runId: string,
    config: EngineRunConfig,
    callbacks: BackendCallbacks
  ): void {
    if (!this.imageData) {
      throw new Error("No image has been loaded into the worker backend");
    }

    this.stop();
    this.runId = runId;
    this.generation = 0;
    this.lastSnapshotAt = 0;
    this.currentConfig = config;
    this.startedAt = performance.now();
    this.currentSeed = config.seed;
    this.geneticAlgorithm = new GeneticAlgorithm(this.imageData, {
      populationSize: config.populationSize,
      mutationRate: config.mutationRate,
      dotCount: config.dotCount,
      elitismRatio: config.elitismRatio,
      random: createSeededRandomSource(config.seed),
    });

    this.scheduleNextBatch(callbacks);
  }

  public pause(): void {
    if (this.batchTimer !== null) {
      clearTimeout(this.batchTimer);
      this.batchTimer = null;
    }
  }

  public stop(): void {
    this.pause();
    this.geneticAlgorithm = null;
    this.runId = null;
    this.generation = 0;
    this.currentConfig = null;
    this.startedAt = 0;
  }

  public hasImage(): boolean {
    return this.imageData !== null;
  }

  public hasActiveRun(): boolean {
    return this.runId !== null && this.geneticAlgorithm !== null;
  }

  public currentGeneration(): number {
    return this.generation;
  }

  public createSnapshotEvent(requestId: string, runId: string): EngineSnapshotEvent {
    if (this.runId !== runId || !this.geneticAlgorithm) {
      throw new Error(`Run ${runId} is not active`);
    }

    return {
      type: "snapshot",
      requestId,
      runId,
      snapshot: {
        generation: this.generation,
        dots: this.serializeBestDots(),
      },
    };
  }

  public exportArtifact(
    _requestId: string,
    _runId: string,
    _format: EngineExportFormat,
    _options?: EngineExportOptions
  ): EngineArtifactEvent {
    throw new Error("Artifact export is only supported by the native WASM backend");
  }

  public activeRunId(): string | null {
    return this.runId;
  }

  public dispose(): void {
    this.stop();
  }

  private scheduleNextBatch(callbacks: BackendCallbacks): void {
    this.batchTimer = setTimeout(() => {
      if (!this.geneticAlgorithm || !this.runId || !this.currentConfig) {
        return;
      }

      const batchStartedAt = performance.now();
      for (let i = 0; i < this.currentConfig.generationsPerBatch; i++) {
        this.geneticAlgorithm.evolve();
        this.generation++;
      }
      const batchDurationMs = performance.now() - batchStartedAt;

      const { bestFitness } = this.getBestIndividualMetrics();
      const metrics = this.createRunMetrics(bestFitness, batchDurationMs);

      callbacks.onProgress({
        type: "progress",
        runId: this.runId,
        generation: this.generation,
        bestFitness,
        status: "running",
        metrics,
      });

      const now = performance.now();
      if (now - this.lastSnapshotAt >= this.currentConfig.previewIntervalMs) {
        this.lastSnapshotAt = now;
        callbacks.onSnapshot(
          this.createSnapshotEvent(
            `snapshot-${this.runId}-${this.generation}`,
            this.runId
          )
        );
      }

      this.scheduleNextBatch(callbacks);
    }, 0);
  }

  private serializeBestDots(): SerializedDot[] {
    if (!this.geneticAlgorithm) {
      return [];
    }

    const population = this.geneticAlgorithm.getPopulation();
    const fittestIndex = population.getFittestIndex();

    return population.population[fittestIndex].dots.map((dot) => ({
      x: dot.x,
      y: dot.y,
      radius: dot.radius,
    }));
  }

  private getBestIndividualMetrics(): { bestFitness: number } {
    if (!this.geneticAlgorithm) {
      return { bestFitness: 0 };
    }

    const population = this.geneticAlgorithm.getPopulation();
    const fittestIndex = population.getFittestIndex();
    return {
      bestFitness: population.population[fittestIndex].fitness,
    };
  }

  private createRunMetrics(
    bestFitness: number,
    batchDurationMs: number
  ): EngineRunMetrics {
    const elapsedMs = Math.max(performance.now() - this.startedAt, 0);
    const performanceWithMemory = performance as Performance & {
      memory?: { usedJSHeapSize?: number };
    };
    const generationsPerSecond =
      batchDurationMs > 0
        ? (this.currentConfig?.generationsPerBatch ?? 0) / (batchDurationMs / 1000)
        : 0;
    const usedHeapBytes =
      typeof performanceWithMemory.memory?.usedJSHeapSize === "number"
        ? performanceWithMemory.memory.usedJSHeapSize
        : undefined;

    return {
      seed: this.currentSeed,
      elapsedMs,
      batchDurationMs,
      generationsPerSecond,
      bestFitness,
      usedHeapBytes,
    };
  }
}
