import { CanvasManager } from "./CanvasManager";
import { GeneticAlgorithm } from "../core/GeneticAlgorithm";
import { CONFIG } from "../utils/config";
import {
  EngineProgressEvent,
  EngineRunConfig,
  EngineSnapshotEvent,
  SerializedDot,
  SerializedImageBuffer,
  TargetPreparedEvent,
  TargetProcessingConfig,
} from "../shared/engineProtocol";
import { RasterImageProcessor } from "../shared/RasterImageProcessor";
import { WasmEngineClient } from "../wasm/WasmEngineClient";

export interface UIElements {
  dotCountElement: HTMLElement;
  dotCountInput: HTMLInputElement;
  blurSlider: HTMLInputElement;
  thresholdSlider: HTMLInputElement;
  blurValueDisplay: HTMLElement;
  thresholdValueDisplay: HTMLElement;
  fileInput: HTMLInputElement;
  startButton: HTMLElement;
  stopButton: HTMLElement;
}

export interface ProcessingState {
  currentImage: HTMLImageElement | null;
  geneticAlgorithm: GeneticAlgorithm | null;
  isEvolutionRunning: boolean;
  generations: number;
  recommendedDotCount: number;
  workerRunId: string | null;
  processingVersion: number;
  animationFrameId?: number;
}

export class EventHandlers {
  private canvasManager: CanvasManager;
  private rasterProcessor = new RasterImageProcessor();
  private engineClient: WasmEngineClient | null = null;
  private elements: UIElements;
  private state: ProcessingState;

  constructor(canvasManager: CanvasManager, elements: UIElements) {
    this.canvasManager = canvasManager;
    this.elements = elements;
    this.state = {
      currentImage: null,
      geneticAlgorithm: null,
      isEvolutionRunning: false,
      generations: 0,
      recommendedDotCount: 0,
      workerRunId: null,
      processingVersion: 0,
    };

    this.initializeEventListeners();
  }

  /**
   * Attach the worker-backed engine client after the app finishes bootstrapping.
   * The worker is optional during the migration, so the UI keeps a clean
   * fallback path to the original main-thread implementation.
   */
  public setEngineClient(engineClient: WasmEngineClient | null): void {
    this.engineClient = engineClient;

    if (!this.engineClient) {
      return;
    }

    this.engineClient.onProgress = this.handleWorkerProgress;
    this.engineClient.onSnapshot = this.handleWorkerSnapshot;

    if (this.state.currentImage && !this.state.isEvolutionRunning) {
      void this.processImage();
    }
  }

  /**
   * Initialize all event listeners
   */
  private initializeEventListeners(): void {
    this.elements.fileInput.addEventListener(
      "change",
      this.handleFileInput.bind(this)
    );

    this.elements.blurSlider.addEventListener(
      "input",
      this.handleBlurChange.bind(this)
    );
    this.elements.thresholdSlider.addEventListener(
      "input",
      this.handleThresholdChange.bind(this)
    );

    this.elements.dotCountInput.addEventListener(
      "change",
      this.handleDotCountChange.bind(this)
    );

    this.elements.startButton.addEventListener(
      "click",
      this.handleStartEvolution.bind(this)
    );
    this.elements.stopButton.addEventListener(
      "click",
      this.handleStopEvolution.bind(this)
    );
  }

  /**
   * Handles file input changes
   */
  private async handleFileInput(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;

    try {
      const image = await this.loadImage(file);
      this.state.currentImage = image;

      this.canvasManager.resizeCanvases(image.width, image.height);

      const imgCtx = this.canvasManager.getImageContext();
      imgCtx.drawImage(image, 0, 0);

      void this.processImage();
    } catch (error) {
      console.error("Error loading image:", error);
    }
  }

  /**
   * Loads an image file and returns a promise
   */
  private loadImage(file: File): Promise<HTMLImageElement> {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      const img = new Image();

      reader.onload = (e) => {
        img.onload = () => resolve(img);
        img.onerror = () => reject(new Error("Failed to load image"));
        img.src = e.target?.result as string;
      };
      reader.onerror = () => reject(new Error("Failed to read file"));
      reader.readAsDataURL(file);
    });
  }

  /**
   * Handles blur slider changes
   */
  private handleBlurChange(event: Event): void {
    const value = parseInt((event.target as HTMLInputElement).value);
    this.elements.blurValueDisplay.textContent = value.toString();
    void this.processImage();
  }

  /**
   * Handles threshold slider changes
   */
  private handleThresholdChange(event: Event): void {
    const value = parseInt((event.target as HTMLInputElement).value);
    this.elements.thresholdValueDisplay.textContent = value.toString();
    void this.processImage();
  }

  /**
   * Handles manual dot count changes
   */
  private handleDotCountChange(event: Event): void {
    const value = parseInt((event.target as HTMLInputElement).value);
    this.state.recommendedDotCount = value;
    this.updateDotCountDisplay();
  }

  /**
   * Processes the current image with current settings
   */
  private async processImage(): Promise<void> {
    if (!this.state.currentImage) {
      return;
    }

    const processingVersion = ++this.state.processingVersion;
    const sourceImage = this.getSourceImageData();
    const processingConfig = this.getProcessingConfig();

    try {
      if (this.engineClient) {
        const preparedTarget = await this.engineClient.prepareTarget(
          this.serializeImageData(sourceImage),
          processingConfig
        );

        if (processingVersion !== this.state.processingVersion) {
          return;
        }

        this.applyPreparedTarget(preparedTarget);
        return;
      }

      const result = this.rasterProcessor.preprocess(sourceImage, processingConfig);

      if (processingVersion !== this.state.processingVersion) {
        return;
      }

      this.applyPreparedTarget({
        type: "target-prepared",
        requestId: `local-${processingVersion}`,
        status: "loaded",
        image: this.serializeImageData(result.imageData),
        stats: result.stats,
      });
    } catch (error) {
      console.error("Error processing image:", error);
    }
  }

  /**
   * Updates the dot count display
   */
  private updateDotCountDisplay(): void {
    this.elements.dotCountElement.textContent =
      `Recommended dot count: ${this.state.recommendedDotCount}` +
      (this.state.generations
        ? `. Generations: ${this.state.generations}`
        : "");
    this.elements.dotCountInput.value =
      this.state.recommendedDotCount.toString();
  }

  /**
   * Handles starting the evolution process
   */
  private async handleStartEvolution(): Promise<void> {
    if (this.state.isEvolutionRunning || !this.state.currentImage) return;

    const runConfig = this.createRunConfig();

    if (this.engineClient) {
      await this.startWorkerEvolution(runConfig);
      return;
    }

    const bwCtx = this.canvasManager.getBWContext();
    const imageData = bwCtx.getImageData(
      0,
      0,
      this.state.currentImage.width,
      this.state.currentImage.height
    );

    this.state.geneticAlgorithm = new GeneticAlgorithm(imageData, {
      populationSize: runConfig.populationSize,
      mutationRate: runConfig.mutationRate,
      dotCount: runConfig.dotCount,
      elitismRatio: runConfig.elitismRatio,
    });

    this.state.isEvolutionRunning = true;
    this.state.workerRunId = null;
    this.state.generations = 0;
    this.updateUIState(true);
    this.startEvolutionLoop();
  }

  /**
   * Runs the evolution animation loop
   */
  private startEvolutionLoop(): void {
    const evolve = () => {
      if (!this.state.isEvolutionRunning || !this.state.geneticAlgorithm)
        return;

      this.state.geneticAlgorithm.evolve();
      this.state.generations++;
      this.updateDotCountDisplay();

      const evolCtx = this.canvasManager.getEvolContext();
      const population = this.state.geneticAlgorithm.getPopulation();
      const fittestIndex = population.getFittestIndex();
      population.drawIndividual(
        population.population[fittestIndex].dots,
        evolCtx
      );

      this.state.animationFrameId = requestAnimationFrame(evolve);
    };

    evolve();
  }

  /**
   * Stops the evolution process
   */
  public stopEvolution(): void {
    this.state.isEvolutionRunning = false;
    if (this.state.animationFrameId) {
      cancelAnimationFrame(this.state.animationFrameId);
    }
    if (this.engineClient && this.state.workerRunId) {
      const runId = this.state.workerRunId;
      this.state.workerRunId = null;
      void this.engineClient.stopRun(runId).catch((error) => {
        console.error("Failed to stop worker evolution:", error);
      });
    }
  }

  /**
   * Updates the UI state
   */
  private updateUIState(isRunning: boolean): void {
    (this.elements.startButton as HTMLButtonElement).disabled = isRunning;
    (this.elements.stopButton as HTMLButtonElement).disabled = !isRunning;
    this.elements.fileInput.disabled = isRunning;
    this.elements.blurSlider.disabled = isRunning;
    this.elements.thresholdSlider.disabled = isRunning;
    this.elements.dotCountInput.disabled = isRunning;

    document.body.classList.toggle("evolution-running", isRunning);
  }

  /**
   * Handles stopping the evolution process
   */
  private handleStopEvolution(): void {
    this.stopEvolution();
    this.updateUIState(false);
  }

  /**
   * Cleans up event listeners and resources
   */
  public dispose(): void {
    this.stopEvolution();
    this.updateUIState(false);
    if (this.engineClient) {
      this.engineClient.onProgress = undefined;
      this.engineClient.onSnapshot = undefined;
    }
  }

  private createRunConfig(): EngineRunConfig {
    return {
      populationSize: CONFIG.GENETIC.DEFAULT_POPULATION_SIZE,
      mutationRate: CONFIG.GENETIC.DEFAULT_MUTATION_RATE,
      dotCount: this.state.recommendedDotCount,
      elitismRatio: CONFIG.GENETIC.ELITISM_RATIO,
      seed: Date.now() >>> 0,
      generationsPerBatch: 1,
      previewIntervalMs: 100,
    };
  }

  private async startWorkerEvolution(
    runConfig: EngineRunConfig
  ): Promise<void> {
    if (!this.engineClient || !this.state.currentImage) {
      return;
    }

    try {
      await this.processImage();

      const runId = `run-${Date.now()}`;
      this.state.workerRunId = runId;
      this.state.geneticAlgorithm = null;
      this.state.isEvolutionRunning = true;
      this.state.generations = 0;
      this.updateUIState(true);
      await this.engineClient.startRun(runId, runConfig);
    } catch (error) {
      this.state.workerRunId = null;
      this.state.isEvolutionRunning = false;
      this.updateUIState(false);
      console.error("Failed to start worker evolution:", error);
    }
  }

  private getSourceImageData(): ImageData {
    const currentImage = this.state.currentImage;
    if (!currentImage) {
      throw new Error("No image is loaded");
    }

    return this.canvasManager.getImageContext().getImageData(
      0,
      0,
      currentImage.width,
      currentImage.height
    );
  }

  private getProcessingConfig(): TargetProcessingConfig {
    return {
      blurAmount: parseInt(this.elements.blurSlider.value),
      threshold: parseInt(this.elements.thresholdSlider.value),
      maxDotCount: CONFIG.IMAGE.MAX_DOT_COUNT,
    };
  }

  private applyPreparedTarget(preparedTarget: TargetPreparedEvent): void {
    const bwCtx = this.canvasManager.getBWContext();
    const processedImageData = new ImageData(
      new Uint8ClampedArray(preparedTarget.image.pixels),
      preparedTarget.image.width,
      preparedTarget.image.height
    );
    bwCtx.putImageData(processedImageData, 0, 0);

    this.state.recommendedDotCount = preparedTarget.stats.recommendedDotCount;
    this.elements.dotCountElement.style.display = "block";
    this.elements.dotCountInput.style.display = "block";
    this.updateDotCountDisplay();
  }

  private serializeImageData(imageData: ImageData): SerializedImageBuffer {
    const pixelCopy = new Uint8ClampedArray(imageData.data);

    return {
      width: imageData.width,
      height: imageData.height,
      format: "rgba8",
      pixels: pixelCopy.buffer,
    };
  }

  private handleWorkerProgress = (event: EngineProgressEvent): void => {
    if (event.runId !== this.state.workerRunId) {
      return;
    }

    this.state.generations = event.generation;
    this.updateDotCountDisplay();
  };

  private handleWorkerSnapshot = (event: EngineSnapshotEvent): void => {
    if (event.runId !== this.state.workerRunId || !event.snapshot.dots) {
      return;
    }

    this.drawDots(event.snapshot.dots);
  };

  private drawDots(dots: SerializedDot[]): void {
    const evolCtx = this.canvasManager.getEvolContext();
    const currentImage = this.state.currentImage;
    if (!currentImage) {
      return;
    }

    evolCtx.clearRect(0, 0, currentImage.width, currentImage.height);
    evolCtx.fillStyle = "white";
    evolCtx.fillRect(0, 0, currentImage.width, currentImage.height);
    evolCtx.fillStyle = "black";

    for (const dot of dots) {
      evolCtx.beginPath();
      evolCtx.arc(dot.x, dot.y, dot.radius, 0, 2 * Math.PI);
      evolCtx.fill();
    }
  }
}
