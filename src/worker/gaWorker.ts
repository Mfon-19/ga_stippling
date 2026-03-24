/// <reference lib="webworker" />

import {
  EngineCommand,
  EngineEvent,
  EngineSnapshotEvent,
  EngineStatus,
  EngineStatusEvent,
} from "../shared/engineProtocol";
import { TypescriptEngineBackend } from "./TypescriptEngineBackend";
import { WasmEngineModule, loadEngineModule } from "../wasm/engineModule";

interface WorkerState {
  status: EngineStatus;
  module: WasmEngineModule | null;
  backend: TypescriptEngineBackend | null;
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

async function handleInitialize(requestId: string): Promise<void> {
  if (!state.module) {
    state.module = await loadEngineModule();
    state.backend = new TypescriptEngineBackend();
  }

  state.status = "idle";
  postEvent({
    type: "ready",
    requestId,
    status: state.status,
    capabilities: state.module.capabilities,
  });
}

function createStatusEvent(requestId: string): EngineStatusEvent {
  return {
    type: "status",
    requestId,
    status: state.status,
    hasImage: state.backend?.hasImage() ?? false,
    activeRunId: state.backend?.activeRunId() ?? null,
  };
}

function createSnapshotEvent(
  requestId: string,
  runId: string
): EngineSnapshotEvent {
  if (!state.backend) {
    throw new Error("Engine worker is not initialized");
  }

  return state.backend.createSnapshotEvent(requestId, runId);
}

function ensureInitialized(): void {
  if (!state.module || !state.backend) {
    throw new Error("Engine worker is not initialized");
  }
}

function ensureImageLoaded(): void {
  ensureInitialized();
  if (!state.backend?.hasImage()) {
    throw new Error("No image has been loaded into the engine worker");
  }
}

function ensureActiveRun(runId: string): void {
  ensureImageLoaded();
  if (state.backend?.activeRunId() !== runId) {
    throw new Error(`Run ${runId} is not active`);
  }
}

function postEvent(
  event: EngineEvent,
  transferables: Transferable[] = []
): void {
  workerScope.postMessage(event, transferables);
}

function toErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : "Unknown worker error";
}

function assertNever(command: never): never {
  throw new Error(`Unhandled worker command: ${JSON.stringify(command)}`);
}
