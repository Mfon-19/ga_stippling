import { CanvasManager } from "./ui/CanvasManager";
import { EventHandlers, UIElements } from "./ui/EventHandlers";
import { CONFIG } from "./utils/config";
import { WasmEngineClient } from "./wasm/WasmEngineClient";

class App {
  private canvasManager!: CanvasManager;
  private eventHandlers!: EventHandlers;
  private engineClient: WasmEngineClient | null = null;

  constructor() {
    this.initializeApp();
  }

  private initializeApp(): void {
    try {
      this.canvasManager = new CanvasManager();
      const elements = this.getUIElements();
      this.eventHandlers = new EventHandlers(this.canvasManager, elements);
      this.setInitialUIState(elements);
      this.setupWindowListeners();

      // Processing and evolution stay disabled until the WASM worker is ready.
      void this.initializeEngineClient();
    } catch (error) {
      this.handleInitializationError(error);
    }
  }

  private getUIElements(): UIElements {
    return {
      dotCountElement: getElement("recommendedDotCount", HTMLElement),
      dotCountInput: getElement("dotCountInput", HTMLInputElement),
      blurSlider: getElement("blurAmount", HTMLInputElement),
      thresholdSlider: getElement("threshold", HTMLInputElement),
      blurValueDisplay: getElement("blurValue", HTMLElement),
      thresholdValueDisplay: getElement("thresholdValue", HTMLElement),
      fileInput: getElement("imgInput", HTMLInputElement),
      startButton: getElement("start", HTMLButtonElement),
      stopButton: getElement("stop", HTMLButtonElement),
      exportSvgButton: getElement("exportSvg", HTMLButtonElement),
      exportPngButton: getElement("exportPng", HTMLButtonElement),
      exportTimelapseButton: getElement("exportTimelapse", HTMLButtonElement),
    };
  }

  private setInitialUIState(elements: UIElements): void {
    elements.blurSlider.value = CONFIG.IMAGE.DEFAULT_BLUR.toString();
    elements.thresholdSlider.value = CONFIG.IMAGE.DEFAULT_THRESHOLD.toString();
    elements.blurValueDisplay.textContent =
      CONFIG.IMAGE.DEFAULT_BLUR.toString();
    elements.thresholdValueDisplay.textContent =
      CONFIG.IMAGE.DEFAULT_THRESHOLD.toString();
    elements.dotCountElement.style.display = "none";
    elements.dotCountInput.style.display = "none";
  }

  private setupWindowListeners(): void {
    window.addEventListener("pagehide", () => {
      this.cleanup();
    });

    window.addEventListener("error", (event) => {
      this.handleError(event.error);
    });
  }

  private handleInitializationError(error: unknown): void {
    console.error("Failed to initialize application:", error);

    const errorMessage =
      error instanceof Error ? error.message : "Unknown error occurred";
    this.showErrorMessage(errorMessage);
  }

  private handleError(error: Error): void {
    console.error("Runtime error:", error);
    this.showErrorMessage(error.message);
  }

  private async initializeEngineClient(): Promise<void> {
    this.engineClient = new WasmEngineClient();

    try {
      await this.engineClient.initialize();
      this.eventHandlers.setEngineClient(this.engineClient);
    } catch (error) {
      console.error(
        "Engine worker bootstrap failed. Processing and evolution are unavailable.",
        error
      );
      this.engineClient.terminate();
      this.engineClient = null;
      this.eventHandlers.setEngineClient(null);
    }
  }

  private showErrorMessage(message: string): void {
    alert(`An error occurred: ${message}`);
  }

  private cleanup(): void {
    if (this.eventHandlers) {
      this.eventHandlers.dispose();
    }
    if (this.engineClient) {
      this.engineClient.terminate();
      this.engineClient = null;
    }
  }
}

function getElement<T extends HTMLElement>(
  id: string,
  type: { new (): T; prototype: T }
): T {
  const element = document.getElementById(id);
  if (!(element instanceof type)) {
    throw new Error(`Required UI element not found or wrong type: ${id}`);
  }
  return element;
}
document.addEventListener("DOMContentLoaded", () => {
  new App();
});

window.addEventListener("beforeunload", (event) => {
  if (document.querySelector(".evolution-running")) {
    event.preventDefault();
  }
});
