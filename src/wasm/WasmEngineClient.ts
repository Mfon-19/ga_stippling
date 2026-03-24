import {
  EngineAckEvent,
  EngineCommand,
  EngineEvent,
  EngineProgressEvent,
  EngineReadyEvent,
  EngineRunConfig,
  EngineSnapshotEvent,
  EngineStatusEvent,
  SerializedImageBuffer,
} from "../shared/engineProtocol";

interface PendingRequest {
  resolve: (event: EngineEvent) => void;
  reject: (error: Error) => void;
}

/**
 * Thin client wrapper around the worker boundary.
 * The UI should talk to this class instead of posting raw messages so the
 * browser-side integration remains stable when the real WASM engine lands.
 */
export class WasmEngineClient {
  private worker: Worker;
  private pendingRequests = new Map<string, PendingRequest>();
  private requestCounter = 0;

  public onProgress?: (event: EngineProgressEvent) => void;
  public onSnapshot?: (event: EngineSnapshotEvent) => void;

  constructor() {
    this.worker = new Worker(new URL("../worker/gaWorker.ts", import.meta.url), {
      type: "module",
    });
    this.worker.addEventListener("message", this.handleMessage);
    this.worker.addEventListener("error", this.handleWorkerError);
  }

  public initialize(): Promise<EngineReadyEvent> {
    return this.sendCommand<EngineReadyEvent>({
      type: "init",
      requestId: this.nextRequestId("init"),
    });
  }

  public getStatus(): Promise<EngineStatusEvent> {
    return this.sendCommand<EngineStatusEvent>({
      type: "request-status",
      requestId: this.nextRequestId("status"),
    });
  }

  public loadImage(image: SerializedImageBuffer): Promise<EngineAckEvent> {
    return this.sendCommand<EngineAckEvent>(
      {
        type: "load-image",
        requestId: this.nextRequestId("image"),
        image,
      },
      [image.pixels]
    );
  }

  public startRun(
    runId: string,
    config: EngineRunConfig
  ): Promise<EngineAckEvent> {
    return this.sendCommand<EngineAckEvent>({
      type: "start-run",
      requestId: this.nextRequestId("start"),
      runId,
      config,
    });
  }

  public pauseRun(runId: string): Promise<EngineAckEvent> {
    return this.sendCommand<EngineAckEvent>({
      type: "pause-run",
      requestId: this.nextRequestId("pause"),
      runId,
    });
  }

  public stopRun(runId: string): Promise<EngineAckEvent> {
    return this.sendCommand<EngineAckEvent>({
      type: "stop-run",
      requestId: this.nextRequestId("stop"),
      runId,
    });
  }

  public requestSnapshot(
    runId: string,
    includeDots = true,
    includeRaster = false
  ): Promise<EngineSnapshotEvent> {
    return this.sendCommand<EngineSnapshotEvent>({
      type: "request-snapshot",
      requestId: this.nextRequestId("snapshot"),
      runId,
      includeDots,
      includeRaster,
    });
  }

  public terminate(): void {
    this.worker.removeEventListener("message", this.handleMessage);
    this.worker.removeEventListener("error", this.handleWorkerError);
    this.rejectPending(new Error("Engine worker terminated"));
    this.worker.terminate();
  }

  private sendCommand<TEvent extends EngineEvent>(
    command: EngineCommand,
    transferables: Transferable[] = []
  ): Promise<TEvent> {
    return new Promise<TEvent>((resolve, reject) => {
      this.pendingRequests.set(command.requestId, {
        resolve: (event) => resolve(event as TEvent),
        reject,
      });
      this.worker.postMessage(command, transferables);
    });
  }

  private handleMessage = (event: MessageEvent<EngineEvent>): void => {
    const message = event.data;

    if (message.type === "progress") {
      this.onProgress?.(message);
      return;
    }

    if (message.type === "snapshot") {
      this.onSnapshot?.(message);
    }

    if ("requestId" in message && message.requestId) {
      const pending = this.pendingRequests.get(message.requestId);
      if (!pending) {
        return;
      }

      this.pendingRequests.delete(message.requestId);

      if (message.type === "error") {
        pending.reject(new Error(message.message));
        return;
      }

      pending.resolve(message);
    }
  };

  private handleWorkerError = (event: ErrorEvent): void => {
    const errorMessage = event.message || "Engine worker bootstrap failed";
    this.rejectPending(new Error(errorMessage));
  };

  private nextRequestId(prefix: string): string {
    this.requestCounter += 1;
    return `${prefix}-${this.requestCounter}`;
  }

  private rejectPending(error: Error): void {
    for (const pending of this.pendingRequests.values()) {
      pending.reject(error);
    }
    this.pendingRequests.clear();
  }
}
