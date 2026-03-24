import {
  EngineArtifactEvent,
  EngineExportFormat,
  EngineExportOptions,
  EngineRunMetrics,
  EngineRunConfig,
  EngineSnapshotEvent,
  TargetPreparedEvent,
  SerializedImageBuffer,
  TargetProcessingConfig,
} from "../shared/engineProtocol";
import { TimelapseFrame, createTextArtifact, renderTimelapseSvg } from "../shared/stippleExport";
import { WasmEngineInstance } from "../wasm/engineModule";
import { BackendCallbacks, WorkerEngineBackend } from "./WorkerEngineBackend";

/**
 * Worker scheduler around the native WASM engine. The native module owns the
 * heavy compute path; this class owns run lifecycle, throttled snapshots, and
 * browser-facing metrics.
 */
export class WasmEngineBackend implements WorkerEngineBackend {
  private runId: string | null = null;
  private generation = 0;
  private batchTimer: ReturnType<typeof setTimeout> | null = null;
  private lastSnapshotAt = 0;
  private currentConfig: EngineRunConfig | null = null;
  private startedAt = 0;
  private currentSeed = 0;
  private preparedWidth = 0;
  private preparedHeight = 0;
  private timelapseFrames: TimelapseFrame[] = [];

  constructor(private engine: WasmEngineInstance) {}

  public prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig,
    requestId: string
  ): TargetPreparedEvent {
    this.stop();
    const preparedTarget = this.engine.prepareTarget(image, processing);
    this.preparedWidth = preparedTarget.image.width;
    this.preparedHeight = preparedTarget.image.height;

    return {
      type: "target-prepared",
      requestId,
      status: "loaded",
      image: preparedTarget.image,
      stats: preparedTarget.stats,
    };
  }

  public startRun(
    runId: string,
    config: EngineRunConfig,
    callbacks: BackendCallbacks
  ): void {
    if (!this.engine.hasImage()) {
      throw new Error("No image has been loaded into the WASM engine");
    }

    this.stop();
    this.runId = runId;
    this.generation = 0;
    this.lastSnapshotAt = 0;
    this.currentConfig = config;
    this.startedAt = performance.now();
    this.currentSeed = config.seed;
    this.timelapseFrames = [];

    this.engine.configure(config);
    this.engine.initializeOptimizer();
    this.captureFrame(0);
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
    this.runId = null;
    this.generation = 0;
    this.currentConfig = null;
    this.startedAt = 0;
    this.timelapseFrames = [];
  }

  public hasImage(): boolean {
    return this.engine.hasImage();
  }

  public activeRunId(): string | null {
    return this.runId;
  }

  public createSnapshotEvent(requestId: string, runId: string): EngineSnapshotEvent {
    if (this.runId !== runId) {
      throw new Error(`Run ${runId} is not active`);
    }

    return {
      type: "snapshot",
      requestId,
      runId,
      snapshot: {
        generation: this.generation,
        dots: this.engine.getBestDots(),
      },
    };
  }

  public exportArtifact(
    requestId: string,
    runId: string,
    format: EngineExportFormat,
    options?: EngineExportOptions
  ): EngineArtifactEvent {
    if (this.runId !== runId) {
      throw new Error(`Run ${runId} is not active`);
    }

    const scale = Math.max(1, Math.floor(options?.scale ?? 4));

    switch (format) {
      case "svg":
        return createTextArtifact(
          requestId,
          runId,
          format,
          "image/svg+xml",
          `stippling-${runId}.svg`,
          this.engine.exportBestSvg(scale)
        );
      case "png":
        return {
          type: "artifact",
          requestId,
          runId,
          format,
          mimeType: "image/png",
          filename: `stippling-${runId}.png`,
          data: this.engine.exportBestPng(scale),
        };
      case "timelapse-svg":
        return createTextArtifact(
          requestId,
          runId,
          format,
          "image/svg+xml",
          `stippling-${runId}-timelapse.svg`,
          renderTimelapseSvg(
            this.timelapseFrames,
            this.preparedWidth,
            this.preparedHeight,
            scale,
            options?.frameDurationMs ?? 120
          )
        );
      default:
        throw new Error(`Unsupported export format: ${format satisfies never}`);
    }
  }

  public dispose(): void {
    this.stop();
    this.engine.dispose();
  }

  private scheduleNextBatch(callbacks: BackendCallbacks): void {
    this.batchTimer = setTimeout(() => {
      if (!this.runId || !this.currentConfig) {
        return;
      }

      const batchStartedAt = performance.now();
      const progress = this.engine.evolveBatch();
      const batchDurationMs = performance.now() - batchStartedAt;
      this.generation = progress.generation;
      const metrics = this.createRunMetrics(progress.bestFitness, batchDurationMs);
      this.captureFrame(progress.generation);

      callbacks.onProgress({
        type: "progress",
        runId: this.runId,
        generation: progress.generation,
        bestFitness: progress.bestFitness,
        status: "running",
        metrics,
      });

      const now = performance.now();
      if (now - this.lastSnapshotAt >= this.currentConfig.previewIntervalMs) {
        this.lastSnapshotAt = now;
        callbacks.onSnapshot(
          this.createSnapshotEvent(
            `snapshot-${this.runId}-${progress.generation}`,
            this.runId
          )
        );
      }

      this.scheduleNextBatch(callbacks);
    }, 0);
  }

  private createRunMetrics(
    bestFitness: number,
    batchDurationMs: number
  ): EngineRunMetrics {
    const elapsedMs = Math.max(performance.now() - this.startedAt, 0);
    const generationsPerSecond =
      batchDurationMs > 0
        ? (this.currentConfig?.generationsPerBatch ?? 0) / (batchDurationMs / 1000)
        : 0;

    return {
      seed: this.currentSeed,
      elapsedMs,
      batchDurationMs,
      generationsPerSecond,
      bestFitness,
      // For the first WASM pass we expose current heap capacity as a useful
      // approximation of native memory pressure.
      usedHeapBytes: this.engine.heapByteLength(),
    };
  }

  private captureFrame(generation: number): void {
    this.timelapseFrames.push({
      generation,
      dots: this.engine.getBestDots(),
    });
  }
}
