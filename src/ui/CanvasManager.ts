import { CANVAS_IDS } from "../utils/config";

export class CanvasManager {
  private imgCanvas!: HTMLCanvasElement;
  private bwCanvas!: HTMLCanvasElement;
  private evolCanvas!: HTMLCanvasElement;

  private imgCtx!: CanvasRenderingContext2D;
  private bwCtx!: CanvasRenderingContext2D;
  private evolCtx!: CanvasRenderingContext2D;

  constructor() {
    this.initializeCanvases();
  }

  private initializeCanvases() {
    this.imgCanvas = document.getElementById(
      CANVAS_IDS.IMAGE
    ) as HTMLCanvasElement;
    this.bwCanvas = document.getElementById(
      CANVAS_IDS.BLACK_WHITE
    ) as HTMLCanvasElement;
    this.evolCanvas = document.getElementById(
      CANVAS_IDS.EVOLUTION
    ) as HTMLCanvasElement;

    this.imgCtx = this.getContext(this.imgCanvas);
    this.bwCtx = this.getContext(this.bwCanvas);
    this.evolCtx = this.getContext(this.evolCanvas);
  }

  private getContext(
    canvas: HTMLCanvasElement,
    willReadFrequently = false
  ): CanvasRenderingContext2D {
    const ctx = canvas.getContext("2d", { willReadFrequently });
    if (!ctx) throw new Error("Failed to get canvas context");
    return ctx;
  }

  public resizeCanvases(width: number, height: number) {
    [this.imgCanvas, this.bwCanvas, this.evolCanvas].forEach((canvas) => {
      canvas.width = width;
      canvas.height = height;
    });
  }
  public getImageContext() {
    return this.imgCtx;
  }
  public getBWContext() {
    return this.bwCtx;
  }
  public getEvolContext() {
    return this.evolCtx;
  }
}
