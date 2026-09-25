import {
  EngineArtifactEvent,
  EngineExportFormat,
  EngineExportOptions,
  EngineProgressEvent,
  EngineRunConfig,
  SerializedImageBuffer,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../shared/engineProtocol";
import { WasmEngineInstance } from "../wasm/engineModule";

// Generations run in time slices with one progress message each, so message and
// DOM traffic scale with wall time, not generation speed. Results don't depend
// on where slices fall.
const SLICE_BUDGET_MS = 16;

const ARTIFACT_FILES: Record<
  EngineExportFormat,
  { mimeType: string; suffix: string }
> = {
  svg: { mimeType: "image/svg+xml", suffix: ".svg" },
  png: { mimeType: "image/png", suffix: ".png" },
  "timelapse-svg": { mimeType: "image/svg+xml", suffix: "-timelapse.svg" },
};

/** Run lifecycle and scheduling around the engine, which does all the compute. */
export class WasmEngineBackend {
  private runId: string | null = null;
  private lastRunId: string | null = null;
  private batchTimer: ReturnType<typeof setTimeout> | null = null;
  private lastPreviewAt = 0;
  private currentConfig: EngineRunConfig | null = null;
  private startedAt = 0;

  constructor(private engine: WasmEngineInstance) {}

  public prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig,
    requestId: string
  ): TargetPreparedEvent {
    this.resetRunState();
    const preparedTarget = this.engine.prepareTarget(image, processing);

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
    onProgress: (event: EngineProgressEvent) => void
  ): void {
    if (!this.engine.hasImage()) {
      throw new Error("No image has been loaded into the WASM engine");
    }

    this.resetRunState();
    this.engine.configure(config);
    this.engine.initializeOptimizer();
    this.runId = runId;
    this.lastRunId = runId;
    this.currentConfig = config;
    this.startedAt = performance.now();
    this.lastPreviewAt = 0;
    this.scheduleNextSlice(onProgress);
  }

  /** The stopped run's result stays exportable. */
  public stop(): void {
    this.cancelScheduledSlice();
    this.runId = null;
    this.currentConfig = null;
  }

  public hasImage(): boolean {
    return this.engine.hasImage();
  }

  /** The active run, or else the last stopped one. */
  public activeRunId(): string | null {
    return this.runId ?? this.lastRunId;
  }

  public exportArtifact(
    requestId: string,
    runId: string,
    format: EngineExportFormat,
    options?: EngineExportOptions
  ): EngineArtifactEvent {
    if (runId !== this.activeRunId()) {
      throw new Error(`Run ${runId} is not active`);
    }

    const scale = Math.max(1, Math.floor(options?.scale ?? 4));
    const frameDurationMs = Math.max(1, Math.floor(options?.frameDurationMs ?? 120));
    const file = ARTIFACT_FILES[format];

    return {
      type: "artifact",
      requestId,
      runId,
      format,
      mimeType: file.mimeType,
      filename: `stippling-${runId}${file.suffix}`,
      data: this.engine.exportArtifact(format, scale, frameDurationMs),
    };
  }

  private scheduleNextSlice(onProgress: (event: EngineProgressEvent) => void): void {
    this.batchTimer = setTimeout(() => {
      const runId = this.runId;
      const config = this.currentConfig;
      if (!runId || !config) {
        return;
      }

      const sliceStartedAt = performance.now();
      let batches = 0;
      let progress = this.engine.evolveBatch();
      batches += 1;
      while (performance.now() - sliceStartedAt < SLICE_BUDGET_MS) {
        progress = this.engine.evolveBatch();
        batches += 1;
      }
      const now = performance.now();
      const sliceMs = now - sliceStartedAt;

      const previewDue = now - this.lastPreviewAt >= config.previewIntervalMs;
      if (previewDue) {
        this.lastPreviewAt = now;
      }

      onProgress({
        type: "progress",
        runId,
        generation: progress.generation,
        metrics: {
          seed: config.seed,
          elapsedMs: now - this.startedAt,
          generationsPerSecond:
            sliceMs > 0 ? (batches * config.generationsPerBatch) / (sliceMs / 1000) : 0,
          bestFitness: progress.bestFitness,
        },
        dots: previewDue ? this.engine.getBestDots() : undefined,
      });

      this.scheduleNextSlice(onProgress);
    }, 0);
  }

  private cancelScheduledSlice(): void {
    if (this.batchTimer !== null) {
      clearTimeout(this.batchTimer);
      this.batchTimer = null;
    }
  }

  private resetRunState(): void {
    this.cancelScheduledSlice();
    this.runId = null;
    this.lastRunId = null;
    this.currentConfig = null;
    this.startedAt = 0;
  }
}
