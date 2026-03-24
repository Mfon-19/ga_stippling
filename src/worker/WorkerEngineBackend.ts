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

/**
 * Worker-side backend contract implemented by the native WASM runtime.
 * The worker talks to this interface so command dispatch stays decoupled from
 * the concrete engine implementation.
 */
export interface BackendCallbacks {
  onProgress(event: EngineProgressEvent): void;
  onSnapshot(event: EngineSnapshotEvent): void;
}

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
