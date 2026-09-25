// UI state and flow: upload, preprocess, run, export. Drawing lives in
// CanvasManager; engine work lives behind WasmEngineClient.
import { CanvasManager } from "./CanvasManager";
import { CONFIG } from "../utils/config";
import {
  EngineExportFormat,
  EngineProgressEvent,
  EngineRunConfig,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../shared/engineProtocol";
import { WasmEngineClient } from "../wasm/WasmEngineClient";

export interface UIElements {
  dotCountElement: HTMLElement;
  dotCountInput: HTMLInputElement;
  blurSlider: HTMLInputElement;
  thresholdSlider: HTMLInputElement;
  blurValueDisplay: HTMLElement;
  thresholdValueDisplay: HTMLElement;
  fileInput: HTMLInputElement;
  startButton: HTMLButtonElement;
  stopButton: HTMLButtonElement;
  exportSvgButton: HTMLButtonElement;
  exportPngButton: HTMLButtonElement;
  exportTimelapseButton: HTMLButtonElement;
}

interface ProcessingState {
  imageVersion: number;
  isEvolutionRunning: boolean;
  generations: number;
  recommendedDotCount: number;
  workerRunId: string | null;
  activeSeed: number | null;
  bestFitness: number | null;
  generationsPerSecond: number | null;
  /** Settings key of the target currently prepared in the engine. */
  preparedKey: string | null;
}

export class EventHandlers {
  private engineClient: WasmEngineClient | null = null;
  private state: ProcessingState = {
    imageVersion: 0,
    isEvolutionRunning: false,
    generations: 0,
    recommendedDotCount: 0,
    workerRunId: null,
    activeSeed: null,
    bestFitness: null,
    generationsPerSecond: null,
    preparedKey: null,
  };
  private processingTimer: ReturnType<typeof setTimeout> | null = null;
  private inFlightProcessing: { key: string; done: Promise<void> } | null = null;

  constructor(
    private canvasManager: CanvasManager,
    private elements: UIElements
  ) {
    this.initializeEventListeners();
    this.updateUIState(false);
  }

  /** Called once the worker is ready (or with null if it failed to start). */
  public setEngineClient(engineClient: WasmEngineClient | null): void {
    this.engineClient = engineClient;
    this.updateUIState(this.state.isEvolutionRunning);

    if (!this.engineClient) {
      return;
    }

    this.engineClient.onProgress = this.handleWorkerProgress;
    if (this.state.imageVersion > 0 && !this.state.isEvolutionRunning) {
      void this.processImage();
    }
  }

  public dispose(): void {
    this.stopEvolution();
    this.cancelScheduledProcessing();
    if (this.engineClient) {
      this.engineClient.onProgress = undefined;
    }
  }

  private initializeEventListeners(): void {
    const { elements } = this;
    elements.fileInput.addEventListener("change", () => void this.handleFileInput());
    elements.blurSlider.addEventListener("input", () =>
      this.handleProcessingSliderInput(elements.blurSlider, elements.blurValueDisplay)
    );
    elements.thresholdSlider.addEventListener("input", () =>
      this.handleProcessingSliderInput(
        elements.thresholdSlider,
        elements.thresholdValueDisplay
      )
    );
    elements.dotCountInput.addEventListener("change", () => this.handleDotCountChange());
    elements.startButton.addEventListener("click", () => void this.handleStartEvolution());
    elements.stopButton.addEventListener("click", () => this.stopEvolution());
    elements.exportSvgButton.addEventListener("click", () => void this.exportArtifact("svg"));
    elements.exportPngButton.addEventListener("click", () => void this.exportArtifact("png"));
    elements.exportTimelapseButton.addEventListener(
      "click",
      () => void this.exportArtifact("timelapse-svg")
    );
  }

  private async handleFileInput(): Promise<void> {
    const file = this.elements.fileInput.files?.[0];
    if (!file) return;

    try {
      const image = await loadImage(file);
      const { width, height } = this.canvasManager.showSourceImage(
        image,
        CONFIG.IMAGE.MAX_DIMENSION
      );
      this.state.imageVersion += 1;
      this.state.preparedKey = null;
      this.updateViewportResolution(width, height);
      void this.processImage();
    } catch (error) {
      console.error("Error loading image:", error);
    }
  }

  // Readout updates immediately; re-preparing waits until input settles.
  private handleProcessingSliderInput(slider: HTMLInputElement, display: HTMLElement): void {
    display.textContent = slider.value;
    this.cancelScheduledProcessing();
    this.processingTimer = setTimeout(() => {
      this.processingTimer = null;
      void this.processImage();
    }, CONFIG.RUN.PROCESSING_DEBOUNCE_MS);
  }

  private handleDotCountChange(): void {
    const value = parseInt(this.elements.dotCountInput.value, 10);
    this.state.recommendedDotCount = Number.isFinite(value) ? Math.max(1, value) : 1;
    this.updateDotCountDisplay();
  }

  private cancelScheduledProcessing(): void {
    if (this.processingTimer !== null) {
      clearTimeout(this.processingTimer);
      this.processingTimer = null;
    }
  }

  private currentProcessingKey(): string {
    return `${this.state.imageVersion}:${this.elements.blurSlider.value}:${this.elements.thresholdSlider.value}`;
  }

  private processImage(): Promise<void> {
    if (this.state.imageVersion === 0 || !this.engineClient) {
      return Promise.resolve();
    }

    const engineClient = this.engineClient;
    const key = this.currentProcessingKey();
    const done = (async () => {
      try {
        const preparedTarget = await engineClient.prepareTarget(
          this.canvasManager.getSourceImage(),
          this.getProcessingConfig()
        );
        // Only the newest request may update the UI; older ones were superseded.
        if (this.inFlightProcessing?.key !== key) {
          return;
        }
        this.state.preparedKey = key;
        this.applyPreparedTarget(preparedTarget);
      } catch (error) {
        console.error("Error processing image:", error);
      }
    })();

    this.inFlightProcessing = { key, done };
    return done;
  }

  // Skips re-preparing when the engine already holds (or is preparing) this target.
  private async ensureTargetPrepared(): Promise<void> {
    this.cancelScheduledProcessing();
    const key = this.currentProcessingKey();
    if (this.state.preparedKey === key) {
      return;
    }
    if (this.inFlightProcessing?.key === key) {
      await this.inFlightProcessing.done;
      if (this.state.preparedKey === key) {
        return;
      }
    }
    await this.processImage();
  }

  private getProcessingConfig(): TargetProcessingConfig {
    return {
      blurAmount: parseInt(this.elements.blurSlider.value, 10),
      threshold: parseInt(this.elements.thresholdSlider.value, 10),
      maxDotCount: CONFIG.IMAGE.MAX_DOT_COUNT,
    };
  }

  private applyPreparedTarget(preparedTarget: TargetPreparedEvent): void {
    this.canvasManager.showPreparedTarget(preparedTarget.image);

    // Preparing a target resets the engine, so an earlier run can no longer export.
    this.state.workerRunId = null;
    this.state.recommendedDotCount = preparedTarget.stats.recommendedDotCount;
    this.elements.dotCountElement.style.display = "block";
    this.elements.dotCountInput.style.display = "block";
    this.updateDotCountDisplay();
    this.updateExportButtons();
  }

  private async handleStartEvolution(): Promise<void> {
    if (this.state.isEvolutionRunning || this.state.imageVersion === 0) return;
    const engineClient = this.engineClient;
    if (!engineClient) {
      console.error("Cannot start evolution without the WASM worker engine");
      return;
    }

    const runConfig = this.createRunConfig();
    const runId = `run-${Date.now()}`;
    this.state.isEvolutionRunning = true;
    this.updateUIState(true);

    try {
      await this.ensureTargetPrepared();
      if (!this.state.isEvolutionRunning) {
        return; // Stopped while the target was still being prepared.
      }
      if (this.state.preparedKey !== this.currentProcessingKey()) {
        throw new Error("The target image could not be prepared");
      }

      this.state.workerRunId = runId;
      this.state.generations = 0;
      this.state.activeSeed = runConfig.seed;
      this.state.bestFitness = null;
      this.state.generationsPerSecond = null;
      await engineClient.startRun(runId, runConfig);
      this.updateExportButtons();
    } catch (error) {
      this.state.workerRunId = null;
      this.state.isEvolutionRunning = false;
      this.updateUIState(false);
      console.error("Failed to start worker evolution:", error);
    }
  }

  // Keeps the run id so the stopped result can still be exported.
  private stopEvolution(): void {
    if (!this.state.isEvolutionRunning) {
      return;
    }

    this.state.isEvolutionRunning = false;
    this.state.bestFitness = null;
    this.state.generationsPerSecond = null;
    if (this.engineClient && this.state.workerRunId) {
      void this.engineClient.stopRun(this.state.workerRunId).catch((error) => {
        console.error("Failed to stop worker evolution:", error);
      });
    }
    this.updateUIState(false);
  }

  private createRunConfig(): EngineRunConfig {
    return {
      populationSize: CONFIG.GENETIC.DEFAULT_POPULATION_SIZE,
      mutationRate: CONFIG.GENETIC.DEFAULT_MUTATION_RATE,
      dotCount: this.state.recommendedDotCount,
      elitismRatio: CONFIG.GENETIC.ELITISM_RATIO,
      seed: Date.now() >>> 0,
      generationsPerBatch: 1,
      previewIntervalMs: CONFIG.RUN.PREVIEW_INTERVAL_MS,
    };
  }

  private handleWorkerProgress = (event: EngineProgressEvent): void => {
    if (event.runId !== this.state.workerRunId || !this.state.isEvolutionRunning) {
      return;
    }

    this.state.generations = event.generation;
    this.state.activeSeed = event.metrics.seed;
    this.state.bestFitness = event.metrics.bestFitness;
    this.state.generationsPerSecond = event.metrics.generationsPerSecond;
    this.updateDotCountDisplay();
    this.updateEvolutionFooter();
    if (event.dots) {
      this.canvasManager.drawDots(event.dots);
    }
  };

  private async exportArtifact(format: EngineExportFormat): Promise<void> {
    if (!this.engineClient || !this.state.workerRunId) {
      return;
    }

    try {
      const artifact = await this.engineClient.exportArtifact(
        this.state.workerRunId,
        format,
        {
          scale: CONFIG.RUN.EXPORT_SCALE,
          frameDurationMs: CONFIG.RUN.TIMELAPSE_FRAME_DURATION_MS,
        }
      );
      downloadBlob(new Blob([artifact.data], { type: artifact.mimeType }), artifact.filename);
    } catch (error) {
      console.error(`Failed to export ${format}:`, error);
    }
  }

  private updateUIState(isRunning: boolean): void {
    const { elements } = this;
    elements.startButton.disabled = isRunning || !this.engineClient;
    elements.stopButton.disabled = !isRunning;
    elements.fileInput.disabled = isRunning;
    elements.blurSlider.disabled = isRunning;
    elements.thresholdSlider.disabled = isRunning;
    elements.dotCountInput.disabled = isRunning;
    this.updateExportButtons();

    document.body.classList.toggle("evolution-running", isRunning);
  }

  private updateExportButtons(): void {
    const canExport = !!this.engineClient && !!this.state.workerRunId;
    this.elements.exportSvgButton.disabled = !canExport;
    this.elements.exportPngButton.disabled = !canExport;
    this.elements.exportTimelapseButton.disabled = !canExport;
  }

  private updateDotCountDisplay(): void {
    const { state } = this;
    this.elements.dotCountElement.textContent =
      `Recommended dot count: ${state.recommendedDotCount}` +
      (state.generations ? `. Generations: ${state.generations}` : "") +
      (state.generationsPerSecond !== null
        ? `. Speed: ${state.generationsPerSecond.toFixed(1)} gen/s`
        : "") +
      (state.bestFitness !== null ? `. Fitness: ${state.bestFitness.toFixed(4)}` : "") +
      (state.activeSeed !== null ? `. Seed: ${state.activeSeed}` : "");
    this.elements.dotCountInput.value = state.recommendedDotCount.toString();
  }

  private updateViewportResolution(width: number, height: number): void {
    document.querySelectorAll<HTMLElement>(".viewport-footer span:first-child").forEach((el) => {
      el.textContent = `${width} × ${height}`;
    });
  }

  private updateEvolutionFooter(): void {
    const footerSpans = document.querySelectorAll<HTMLElement>(
      "#viewportEvolution .viewport-footer span"
    );
    if (footerSpans.length >= 2) {
      footerSpans[0].textContent = `Gen ${this.state.generations}`;
      footerSpans[1].textContent =
        this.state.bestFitness !== null ? `Fitness ${this.state.bestFitness.toFixed(4)}` : "—";
    }
  }
}

function loadImage(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const image = new Image();
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Failed to load image"));
    };
    image.src = url;
  });
}

function downloadBlob(blob: Blob, filename: string): void {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
}
