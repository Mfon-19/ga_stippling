import { CanvasManager } from "./ui/CanvasManager";
import { EventHandlers, UIElements } from "./ui/EventHandlers";
import { CONFIG } from "./utils/config";

class App {
  private canvasManager!: CanvasManager;
  private eventHandlers!: EventHandlers;

  constructor() {
    this.initializeApp();
  }

  /**
   * Initialize the application
   */
  private initializeApp(): void {
    try {
      // Initialize canvas manager
      this.canvasManager = new CanvasManager();

      // Get and validate UI elements
      const elements = this.getUIElements();

      // Initialize event handlers
      this.eventHandlers = new EventHandlers(this.canvasManager, elements);

      // Set initial UI values
      this.setInitialUIState(elements);

      // Add window cleanup
      this.setupWindowListeners();
    } catch (error) {
      this.handleInitializationError(error);
    }
  }

  /**
   * Get and validate all required UI elements
   */
  private getUIElements(): UIElements {
    const elements = {
      dotCountElement: this.getRequiredElement("recommendedDotCount"),
      dotCountInput: this.getRequiredElement(
        "dotCountInput"
      ) as HTMLInputElement,
      blurSlider: this.getRequiredElement("blurAmount") as HTMLInputElement,
      thresholdSlider: this.getRequiredElement("threshold") as HTMLInputElement,
      blurValueDisplay: this.getRequiredElement("blurValue"),
      thresholdValueDisplay: this.getRequiredElement("thresholdValue"),
      fileInput: this.getRequiredElement("imgInput") as HTMLInputElement,
      startButton: this.getRequiredElement("start"),
      stopButton: this.getRequiredElement("stop"),
    };

    this.validateUIElements(elements);
    return elements;
  }

  /**
   * Get required DOM element with error handling
   */
  private getRequiredElement(id: string): HTMLElement {
    const element = document.getElementById(id);
    if (!element) {
      throw new Error(`Required UI element not found: ${id}`);
    }
    return element;
  }

  /**
   * Validate that all UI elements are of correct type
   */
  private validateUIElements(elements: UIElements): void {
    if (!(elements.dotCountInput instanceof HTMLInputElement)) {
      throw new Error("dotCountInput must be an input element");
    }
    if (!(elements.blurSlider instanceof HTMLInputElement)) {
      throw new Error("blurSlider must be an input element");
    }
    if (!(elements.thresholdSlider instanceof HTMLInputElement)) {
      throw new Error("thresholdSlider must be an input element");
    }
    if (!(elements.fileInput instanceof HTMLInputElement)) {
      throw new Error("fileInput must be an input element");
    }
  }

  /**
   * Set initial UI state
   */
  private setInitialUIState(elements: UIElements): void {
    // Set initial slider values
    elements.blurSlider.value = CONFIG.IMAGE.DEFAULT_BLUR.toString();
    elements.thresholdSlider.value = CONFIG.IMAGE.DEFAULT_THRESHOLD.toString();

    // Set initial display values
    elements.blurValueDisplay.textContent =
      CONFIG.IMAGE.DEFAULT_BLUR.toString();
    elements.thresholdValueDisplay.textContent =
      CONFIG.IMAGE.DEFAULT_THRESHOLD.toString();

    // Hide dot count elements initially
    elements.dotCountElement.style.display = "none";
    elements.dotCountInput.style.display = "none";
  }

  /**
   * Setup window event listeners
   */
  private setupWindowListeners(): void {
    window.addEventListener("unload", () => {
      this.cleanup();
    });

    window.addEventListener("error", (event) => {
      this.handleError(event.error);
    });
  }

  /**
   * Handle initialization errors
   */
  private handleInitializationError(error: unknown): void {
    console.error("Failed to initialize application:", error);

    const errorMessage =
      error instanceof Error ? error.message : "Unknown error occurred";
    this.showErrorMessage(errorMessage);
  }

  /**
   * Handle runtime errors
   */
  private handleError(error: Error): void {
    console.error("Runtime error:", error);
    this.showErrorMessage(error.message);
  }

  /**
   * Show error message to user
   */
  private showErrorMessage(message: string): void {
    alert(`An error occurred: ${message}`);
  }

  /**
   * Cleanup resources
   */
  private cleanup(): void {
    if (this.eventHandlers) {
      this.eventHandlers.dispose();
    }
  }
}

// Initialize the application when the DOM is ready
document.addEventListener("DOMContentLoaded", () => {
  new App();
});

window.addEventListener("beforeunload", (event) => {
  if (document.querySelector(".evolution-running")) {
    event.preventDefault();
  }
});