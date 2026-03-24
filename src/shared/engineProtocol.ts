/**
 * Shared contract between the UI, the worker, and the future C++/WASM engine.
 * Keeping these message shapes in one place makes the browser-to-engine
 * boundary explicit before the native backend exists.
 */

export type EngineStatus =
  | "booting"
  | "idle"
  | "loaded"
  | "running"
  | "paused"
  | "error";

export type EngineBackend = "typescript" | "wasm";

export interface EngineCapabilities {
  backend: EngineBackend;
  incrementalFitness: boolean;
  multiscale: boolean;
  benchmarkMode: boolean;
  exportSvg: boolean;
  exportPng: boolean;
}

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

export interface EngineSnapshot {
  generation: number;
  dots?: SerializedDot[];
  raster?: ArrayBuffer;
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

export interface PauseRunCommand extends BaseCommand {
  type: "pause-run";
  runId: string;
}

export interface StopRunCommand extends BaseCommand {
  type: "stop-run";
  runId: string;
}

export interface RequestSnapshotCommand extends BaseCommand {
  type: "request-snapshot";
  runId: string;
  includeDots?: boolean;
  includeRaster?: boolean;
}

export interface RequestStatusCommand extends BaseCommand {
  type: "request-status";
}

export type EngineCommand =
  | InitializeEngineCommand
  | PrepareTargetCommand
  | StartRunCommand
  | PauseRunCommand
  | StopRunCommand
  | RequestSnapshotCommand
  | RequestStatusCommand;

export interface EngineReadyEvent {
  type: "ready";
  requestId: string;
  status: EngineStatus;
  capabilities: EngineCapabilities;
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

export interface EngineStatusEvent {
  type: "status";
  requestId: string;
  status: EngineStatus;
  hasImage: boolean;
  activeRunId: string | null;
}

export interface EngineProgressEvent {
  type: "progress";
  runId: string;
  generation: number;
  bestFitness: number;
  status: EngineStatus;
}

export interface EngineSnapshotEvent {
  type: "snapshot";
  requestId: string;
  runId: string;
  snapshot: EngineSnapshot;
}

export interface EngineErrorEvent {
  type: "error";
  requestId?: string;
  message: string;
  recoverable: boolean;
}

export type EngineEvent =
  | EngineReadyEvent
  | EngineAckEvent
  | TargetPreparedEvent
  | EngineStatusEvent
  | EngineProgressEvent
  | EngineSnapshotEvent
  | EngineErrorEvent;
