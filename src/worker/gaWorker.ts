/// <reference lib="webworker" />

// Worker entry: boots the WASM engine on `init` and routes commands to it.
import {
  EngineCommand,
  EngineEvent,
  EngineStatus,
} from "../shared/engineProtocol";
import { loadEngineModule } from "../wasm/engineModule";
import { WasmEngineBackend } from "./WasmEngineBackend";

const workerScope = self as DedicatedWorkerGlobalScope;

let status: EngineStatus = "booting";
let backend: WasmEngineBackend | null = null;

workerScope.addEventListener("message", (event: MessageEvent<EngineCommand>) => {
  void handleCommand(event.data);
});

async function handleCommand(command: EngineCommand): Promise<void> {
  try {
    switch (command.type) {
      case "init":
        if (!backend) {
          const module = await loadEngineModule();
          backend = new WasmEngineBackend(module.createEngine());
        }
        status = "idle";
        postEvent({ type: "ready", requestId: command.requestId, status });
        return;
      case "prepare-target": {
        const preparedTarget = requireBackend().prepareTarget(
          command.image,
          command.processing,
          command.requestId
        );
        status = "loaded";
        postEvent({ ...preparedTarget, status }, [preparedTarget.image.pixels]);
        return;
      }
      case "start-run": {
        const activeBackend = requireBackend();
        if (!activeBackend.hasImage()) {
          throw new Error("No image has been loaded into the engine worker");
        }
        activeBackend.startRun(command.runId, command.config, (event) => {
          postEvent(event);
        });
        status = "running";
        postEvent({ type: "ack", requestId: command.requestId, status });
        return;
      }
      case "stop-run": {
        const activeBackend = requireActiveRun(command.runId);
        activeBackend.stop();
        status = "loaded";
        postEvent({ type: "ack", requestId: command.requestId, status });
        return;
      }
      case "export-artifact": {
        const artifact = requireActiveRun(command.runId).exportArtifact(
          command.requestId,
          command.runId,
          command.format,
          command.options
        );
        postEvent(artifact, [artifact.data]);
        return;
      }
      default:
        assertNever(command);
    }
  } catch (error) {
    status = "error";
    postEvent({
      type: "error",
      requestId: command.requestId,
      message: error instanceof Error ? error.message : "Unknown worker error",
      recoverable: true,
    });
  }
}

function requireBackend(): WasmEngineBackend {
  if (!backend) {
    throw new Error("Engine worker is not initialized");
  }
  return backend;
}

function requireActiveRun(runId: string): WasmEngineBackend {
  const activeBackend = requireBackend();
  if (activeBackend.activeRunId() !== runId) {
    throw new Error(`Run ${runId} is not active`);
  }
  return activeBackend;
}

function postEvent(event: EngineEvent, transferables: Transferable[] = []): void {
  workerScope.postMessage(event, transferables);
}

function assertNever(command: never): never {
  throw new Error(`Unhandled worker command: ${JSON.stringify(command)}`);
}
