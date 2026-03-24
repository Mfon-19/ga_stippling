/// <reference lib="webworker" />

import {
  EngineCommand,
  EngineEvent,
  EngineSnapshotEvent,
  EngineStatus,
  EngineStatusEvent,
  SerializedImageBuffer,
} from "../shared/engineProtocol";
import { WasmEngineModule, loadEngineModule } from "../wasm/engineModule";

interface WorkerState {
  status: EngineStatus;
  module: WasmEngineModule | null;
  image: SerializedImageBuffer | null;
  activeRunId: string | null;
  generation: number;
}

const workerScope = self as DedicatedWorkerGlobalScope;

const state: WorkerState = {
  status: "booting",
  module: null,
  image: null,
  activeRunId: null,
  generation: 0,
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
      case "load-image":
        ensureInitialized();
        state.image = command.image;
        state.activeRunId = null;
        state.generation = 0;
        state.status = "loaded";
        postEvent({
          type: "ack",
          requestId: command.requestId,
          status: state.status,
        });
        return;
      case "start-run":
        ensureImageLoaded();
        state.activeRunId = command.runId;
        state.generation = 0;
        state.status = "running";
        postEvent({
          type: "ack",
          requestId: command.requestId,
          status: state.status,
        });
        postEvent({
          type: "progress",
          runId: command.runId,
          generation: state.generation,
          bestFitness: 0,
          status: state.status,
        });
        return;
      case "pause-run":
        ensureActiveRun(command.runId);
        state.status = "paused";
        postEvent({
          type: "ack",
          requestId: command.requestId,
          status: state.status,
        });
        return;
      case "stop-run":
        ensureActiveRun(command.runId);
        state.activeRunId = null;
        state.generation = 0;
        state.status = state.image ? "loaded" : "idle";
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
    hasImage: state.image !== null,
    activeRunId: state.activeRunId,
  };
}

function createSnapshotEvent(
  requestId: string,
  runId: string
): EngineSnapshotEvent {
  return {
    type: "snapshot",
    requestId,
    runId,
    snapshot: {
      generation: state.generation,
    },
  };
}

function ensureInitialized(): void {
  if (!state.module) {
    throw new Error("Engine worker is not initialized");
  }
}

function ensureImageLoaded(): void {
  ensureInitialized();
  if (!state.image) {
    throw new Error("No image has been loaded into the engine worker");
  }
}

function ensureActiveRun(runId: string): void {
  ensureImageLoaded();
  if (state.activeRunId !== runId) {
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
