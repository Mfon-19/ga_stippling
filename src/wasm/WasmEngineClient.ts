import {
  EngineAckEvent,
  EngineArtifactEvent,
  EngineCommand,
  EngineEvent,
  EngineExportFormat,
  EngineExportOptions,
  EngineProgressEvent,
  EngineReadyEvent,
  EngineRunConfig,
  SerializedImageBuffer,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../shared/engineProtocol";

interface PendingRequest {
  resolve: (event: EngineEvent) => void;
  reject: (error: Error) => void;
}

/** Promise-based client for the engine worker; replies are matched by requestId. */
export class WasmEngineClient {
  private worker: Worker;
  private pendingRequests = new Map<string, PendingRequest>();
  private requestCounter = 0;

  public onProgress?: (event: EngineProgressEvent) => void;

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

  public prepareTarget(
    image: SerializedImageBuffer,
    processing: TargetProcessingConfig
  ): Promise<TargetPreparedEvent> {
    return this.sendCommand<TargetPreparedEvent>(
      {
        type: "prepare-target",
        requestId: this.nextRequestId("target"),
        image,
        processing,
      },
      [image.pixels]
    );
  }

  public startRun(runId: string, config: EngineRunConfig): Promise<EngineAckEvent> {
    return this.sendCommand<EngineAckEvent>({
      type: "start-run",
      requestId: this.nextRequestId("start"),
      runId,
      config,
    });
  }

  public stopRun(runId: string): Promise<EngineAckEvent> {
    return this.sendCommand<EngineAckEvent>({
      type: "stop-run",
      requestId: this.nextRequestId("stop"),
      runId,
    });
  }

  public exportArtifact(
    runId: string,
    format: EngineExportFormat,
    options?: EngineExportOptions
  ): Promise<EngineArtifactEvent> {
    return this.sendCommand<EngineArtifactEvent>({
      type: "export-artifact",
      requestId: this.nextRequestId("artifact"),
      runId,
      format,
      options,
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

    if (!message.requestId) {
      return;
    }

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
