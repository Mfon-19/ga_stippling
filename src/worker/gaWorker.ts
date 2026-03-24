/// <reference lib="webworker" />

/**
 * Dedicated worker entrypoint that owns the browser-to-engine command loop.
 * It lazily boots the WASM module, routes commands to the backend, and sends
 * progress, snapshots, errors, and export artifacts back to the main thread.
 */
import {
  EngineCommand,
  EngineEvent,
  EngineArtifactEvent,
  EngineSnapshotEvent,
  EngineStatus,
  EngineStatusEvent,
} from "../shared/engineProtocol";
import { WasmEngineModule, loadEngineModule } from "../wasm/engineModule";
import { WasmEngineBackend } from "./WasmEngineBackend";
import { WorkerEngineBackend } from "./WorkerEngineBackend";

interface WorkerState {
  status: EngineStatus;
  module: WasmEngineModule | null;
  backend: WorkerEngineBackend | null;
}

const workerScope = self as DedicatedWorkerGlobalScope;

const state: WorkerState = {
  status: "booting",
  module: null,
  backend: null,
};

workerScope.addEventListener("message", (event: MessageEvent<EngineCommand>) => {
  void handleCommand(event.data);
});

/** Dispatches one command from the main thread to the worker backend. */
async function handleCommand(command: EngineCommand): Promise<void> {
  try {
    switch (command.type) {
      case "init":
        await handleInitialize(command.requestId);
        return;
      case "prepare-target": {
        ensureInitialized();
        const preparedTarget = state.backend?.prepareTarget(
          command.image,
          command.processing,
          command.requestId
        );
        state.status = "loaded";
        if (!preparedTarget) {
          throw new Error("Worker backend failed to prepare the target image");
        }
        preparedTarget.status = state.status;
        postEvent(preparedTarget, [preparedTarget.image.pixels]);
        return;
      }
      case "start-run":
        ensureImageLoaded();
        state.backend?.startRun(command.runId, command.config, {
          onProgress: (event) => {
            state.status = "running";
            postEvent(event);
          },
          onSnapshot: (event) => {
            postEvent(event);
          },
        });
        state.status = "running";
        postEvent({
          type: "ack",
          requestId: command.requestId,
          status: state.status,
        });
        return;
      case "pause-run":
        ensureActiveRun(command.runId);
        state.backend?.pause();
        state.status = "paused";
        postEvent({
          type: "ack",
          requestId: command.requestId,
          status: state.status,
        });
        return;
      case "stop-run":
        ensureActiveRun(command.runId);
        state.backend?.stop();
        state.status = state.backend?.hasImage() ? "loaded" : "idle";
        postEvent({
          type: "ack",
          requestId: command.requestId,
          status: state.status,
        });
        return;
      case "request-snapshot":
        ensureActiveRun(command.runId);
        postEvent(createSnapshotEvent(command.requestId, command.runId));
        return;
      case "request-status":
        postEvent(createStatusEvent(command.requestId));
        return;
      case "export-artifact":
        ensureActiveRun(command.runId);
        postArtifact(
          state.backend?.exportArtifact(
            command.requestId,
            command.runId,
            command.format,
            command.options
          ) ?? {
            type: "artifact",
            requestId: command.requestId,
            runId: command.runId,
            format: command.format,
            mimeType: "application/octet-stream",
            filename: "artifact.bin",
            data: new ArrayBuffer(0),
          }
        );
        return;
      default:
        assertNever(command);
    }
  } catch (error) {
    state.status = "error";
    postEvent({
      type: "error",
      requestId: command.requestId,
      message: toErrorMessage(error),
      recoverable: true,
    });
  }
}

/** Lazily initializes the WASM module and backend on the first `init` request. */
async function handleInitialize(requestId: string): Promise<void> {
  if (!state.module) {
    state.module = await loadEngineModule();
    state.backend = new WasmEngineBackend(state.module.createEngine());
  }

  state.status = "idle";
  postEvent({
    type: "ready",
    requestId,
    status: state.status,
    capabilities: state.module.capabilities,
  });
}

/** Builds a worker status event for the current backend state. */
function createStatusEvent(requestId: string): EngineStatusEvent {
  return {
    type: "status",
    requestId,
    status: state.status,
    hasImage: state.backend?.hasImage() ?? false,
    activeRunId: state.backend?.activeRunId() ?? null,
  };
}

/** Asks the backend for the latest snapshot of the active run. */
function createSnapshotEvent(
  requestId: string,
  runId: string
): EngineSnapshotEvent {
  if (!state.backend) {
    throw new Error("Engine worker is not initialized");
  }

  return state.backend.createSnapshotEvent(requestId, runId);
}

/** Ensures the worker module and backend have been initialized. */
function ensureInitialized(): void {
  if (!state.module || !state.backend) {
    throw new Error("Engine worker is not initialized");
  }
}

/** Ensures a prepared image exists before run commands proceed. */
function ensureImageLoaded(): void {
  ensureInitialized();
  if (!state.backend?.hasImage()) {
    throw new Error("No image has been loaded into the engine worker");
  }
}

/** Ensures the requested run id matches the backend's active run. */
function ensureActiveRun(runId: string): void {
  ensureImageLoaded();
  if (state.backend?.activeRunId() !== runId) {
    throw new Error(`Run ${runId} is not active`);
  }
}

/** Posts a structured worker event back to the main thread. */
function postEvent(
  event: EngineEvent,
  transferables: Transferable[] = []
): void {
  workerScope.postMessage(event, transferables);
}

/** Posts an artifact event and transfers the artifact bytes. */
function postArtifact(event: EngineArtifactEvent): void {
  postEvent(event, [event.data]);
}

/** Converts unknown thrown values into a stable error string. */
function toErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : "Unknown worker error";
}

/** Makes the command switch exhaustive. */
function assertNever(command: never): never {
  throw new Error(`Unhandled worker command: ${JSON.stringify(command)}`);
}
