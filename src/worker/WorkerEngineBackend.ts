import {
  EngineArtifactEvent,
  EngineExportFormat,
  EngineExportOptions,
  EngineProgressEvent,
  EngineRunConfig,
  EngineSnapshotEvent,
  SerializedImageBuffer,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../shared/engineProtocol";

export interface BackendCallbacks {
  onProgress(event: EngineProgressEvent): void;
  onSnapshot(event: EngineSnapshotEvent): void;
}

/**
 * Shared worker-facing backend contract used by both the transitional
 * TypeScript engine and the native WASM engine.
 */
export interface WorkerEngineBackend {
  prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig,
    requestId: string
  ): TargetPreparedEvent;
  startRun(runId: string, config: EngineRunConfig, callbacks: BackendCallbacks): void;
  pause(): void;
  stop(): void;
  hasImage(): boolean;
  activeRunId(): string | null;
  createSnapshotEvent(requestId: string, runId: string): EngineSnapshotEvent;
  exportArtifact(
    requestId: string,
    runId: string,
    format: EngineExportFormat,
    options?: EngineExportOptions
  ): EngineArtifactEvent;
  dispose(): void;
}
