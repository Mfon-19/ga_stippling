// Messages between the UI thread and the engine worker.

export type EngineStatus = "booting" | "idle" | "loaded" | "running" | "error";

export interface SerializedImageBuffer {
  width: number;
  height: number;
  format: "rgba8";
  pixels: ArrayBuffer;
}

export interface TargetProcessingConfig {
  blurAmount: number;
  threshold: number;
  maxDotCount: number;
}

export interface TargetStats {
  blackPixels: number;
  totalPixels: number;
  blackPercentage: number;
  recommendedDotCount: number;
}

export interface SerializedDot {
  x: number;
  y: number;
  radius: number;
}

export interface EngineRunConfig {
  populationSize: number;
  mutationRate: number;
  dotCount: number;
  elitismRatio: number;
  seed: number;
  generationsPerBatch: number;
  previewIntervalMs: number;
}

export type EngineExportFormat = "svg" | "png" | "timelapse-svg";

export interface EngineExportOptions {
  scale?: number;
  frameDurationMs?: number;
}

export interface EngineRunMetrics {
  seed: number;
  elapsedMs: number;
  generationsPerSecond: number;
  bestFitness: number;
}

interface BaseCommand {
  requestId: string;
}

export interface InitializeEngineCommand extends BaseCommand {
  type: "init";
}

export interface PrepareTargetCommand extends BaseCommand {
  type: "prepare-target";
  image: SerializedImageBuffer;
  processing: TargetProcessingConfig;
}

export interface StartRunCommand extends BaseCommand {
  type: "start-run";
  runId: string;
  config: EngineRunConfig;
}

export interface StopRunCommand extends BaseCommand {
  type: "stop-run";
  runId: string;
}

export interface ExportArtifactCommand extends BaseCommand {
  type: "export-artifact";
  runId: string;
  format: EngineExportFormat;
  options?: EngineExportOptions;
}

export type EngineCommand =
  | InitializeEngineCommand
  | PrepareTargetCommand
  | StartRunCommand
  | StopRunCommand
  | ExportArtifactCommand;

export interface EngineReadyEvent {
  type: "ready";
  requestId: string;
  status: EngineStatus;
}

export interface EngineAckEvent {
  type: "ack";
  requestId: string;
  status: EngineStatus;
}

export interface TargetPreparedEvent {
  type: "target-prepared";
  requestId: string;
  status: EngineStatus;
  image: SerializedImageBuffer;
  stats: TargetStats;
}

/** Posted once per time slice; `dots` only when the preview interval has passed. */
export interface EngineProgressEvent {
  type: "progress";
  runId: string;
  generation: number;
  metrics: EngineRunMetrics;
  dots?: SerializedDot[];
}

export interface EngineErrorEvent {
  type: "error";
  requestId?: string;
  message: string;
  recoverable: boolean;
}

export interface EngineArtifactEvent {
  type: "artifact";
  requestId: string;
  runId: string;
  format: EngineExportFormat;
  mimeType: string;
  filename: string;
  data: ArrayBuffer;
}

export type EngineEvent =
  | EngineReadyEvent
  | EngineAckEvent
  | TargetPreparedEvent
  | EngineProgressEvent
  | EngineErrorEvent
  | EngineArtifactEvent;
