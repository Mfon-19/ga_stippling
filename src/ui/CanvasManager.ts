import { SerializedDot, SerializedImageBuffer } from "../shared/engineProtocol";
import { CANVAS_IDS } from "../utils/config";

/** Source, target-preview, and evolution canvases, all at the downscaled working size. */
export class CanvasManager {
  private imgCanvas: HTMLCanvasElement;
  private bwCanvas: HTMLCanvasElement;
  private evolCanvas: HTMLCanvasElement;

  private imgCtx: CanvasRenderingContext2D;
  private bwCtx: CanvasRenderingContext2D;
  private evolCtx: CanvasRenderingContext2D;

  constructor() {
    this.imgCanvas = getCanvas(CANVAS_IDS.IMAGE);
    this.bwCanvas = getCanvas(CANVAS_IDS.BLACK_WHITE);
    this.evolCanvas = getCanvas(CANVAS_IDS.EVOLUTION);

    this.imgCtx = getContext(this.imgCanvas, true);
    this.bwCtx = getContext(this.bwCanvas);
    this.evolCtx = getContext(this.evolCanvas);
  }

  /** Downscales to a longest edge of `maxDimension` and sizes every canvas to match. */
  public showSourceImage(
    image: HTMLImageElement,
    maxDimension: number
  ): { width: number; height: number } {
    const scale = Math.min(1, maxDimension / Math.max(image.width, image.height));
    const width = Math.max(1, Math.round(image.width * scale));
    const height = Math.max(1, Math.round(image.height * scale));

    for (const canvas of [this.imgCanvas, this.bwCanvas, this.evolCanvas]) {
      canvas.width = width;
      canvas.height = height;
    }
    this.imgCtx.drawImage(image, 0, 0, width, height);
    return { width, height };
  }

  public getSourceImage(): SerializedImageBuffer {
    const { width, height } = this.imgCanvas;
    const imageData = this.imgCtx.getImageData(0, 0, width, height);
    return {
      width,
      height,
      format: "rgba8",
      pixels: imageData.data.buffer,
    };
  }

  public showPreparedTarget(image: SerializedImageBuffer): void {
    this.bwCtx.putImageData(
      new ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height),
      0,
      0
    );
  }

  // Same circles as the SVG/PNG export.
  public drawDots(dots: SerializedDot[]): void {
    const { width, height } = this.evolCanvas;
    this.evolCtx.fillStyle = "white";
    this.evolCtx.fillRect(0, 0, width, height);
    this.evolCtx.fillStyle = "black";

    for (const dot of dots) {
      this.evolCtx.beginPath();
      this.evolCtx.arc(dot.x, dot.y, dot.radius, 0, 2 * Math.PI);
      this.evolCtx.fill();
    }
  }
}

function getCanvas(id: string): HTMLCanvasElement {
  const canvas = document.getElementById(id);
  if (!(canvas instanceof HTMLCanvasElement)) {
    throw new Error(`Required canvas not found: ${id}`);
  }
  return canvas;
}

function getContext(
  canvas: HTMLCanvasElement,
  willReadFrequently = false
): CanvasRenderingContext2D {
  const ctx = canvas.getContext("2d", { willReadFrequently });
  if (!ctx) throw new Error("Failed to get canvas context");
  return ctx;
}
