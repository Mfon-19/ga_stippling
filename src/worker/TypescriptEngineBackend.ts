import { GeneticAlgorithm } from "../core/GeneticAlgorithm";
import { RasterImageProcessor } from "../shared/RasterImageProcessor";
import {
  EngineProgressEvent,
  EngineRunConfig,
  EngineSnapshotEvent,
  SerializedDot,
  SerializedImageBuffer,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../shared/engineProtocol";

interface BackendCallbacks {
  onProgress(event: EngineProgressEvent): void;
  onSnapshot(event: EngineSnapshotEvent): void;
}

/**
 * Transitional backend that runs the existing TypeScript optimizer inside the
 * worker. The public surface is shaped like the future native backend so the
 * UI can move off the main thread before the C++ port is finished.
 */
export class TypescriptEngineBackend {
  private rasterProcessor = new RasterImageProcessor();
  private imageData: ImageData | null = null;
  private geneticAlgorithm: GeneticAlgorithm | null = null;
  private runId: string | null = null;
  private generation = 0;
  private batchTimer: ReturnType<typeof setTimeout> | null = null;
  private lastSnapshotAt = 0;
  private currentConfig: EngineRunConfig | null = null;

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
    this.geneticAlgorithm = new GeneticAlgorithm(this.imageData, {
      populationSize: config.populationSize,
      mutationRate: config.mutationRate,
      dotCount: config.dotCount,
      elitismRatio: config.elitismRatio,
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

  public activeRunId(): string | null {
    return this.runId;
  }

  private scheduleNextBatch(callbacks: BackendCallbacks): void {
    this.batchTimer = setTimeout(() => {
      if (!this.geneticAlgorithm || !this.runId || !this.currentConfig) {
        return;
      }

      for (let i = 0; i < this.currentConfig.generationsPerBatch; i++) {
        this.geneticAlgorithm.evolve();
        this.generation++;
      }

      const { bestFitness } = this.getBestIndividualMetrics();

      callbacks.onProgress({
        type: "progress",
        runId: this.runId,
        generation: this.generation,
        bestFitness,
        status: "running",
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
}
