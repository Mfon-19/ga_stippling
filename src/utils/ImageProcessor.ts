import { CONFIG } from "./config";

export interface ImageStats {
  blackPixels: number;
  totalPixels: number;
  blackPercentage: number;
  recommendedDotCount: number;
}

export class ImageProcessor {
  private ctx: CanvasRenderingContext2D;
  private width: number;
  private height: number;

  constructor(ctx: CanvasRenderingContext2D, width: number, height: number) {
    this.ctx = ctx;
    this.width = width;
    this.height = height;
  }

  /**
   * Converts an image to black and white with blur and threshold effects
   * @param sourceCtx The source canvas context containing the original image
   * @param blurAmount The amount of blur to apply (radius)
   * @param threshold The threshold value for black/white conversion (0-255)
   * @returns The processed ImageData
   */
  public convertToBlackAndWhite(
    sourceCtx: CanvasRenderingContext2D,
    blurAmount: number,
    threshold: number
  ): ImageData {
    const imageData = sourceCtx.getImageData(0, 0, this.width, this.height);
    const data = imageData.data;

    this.convertToGrayscale(data);
    this.applyBlur(data, blurAmount);
    this.applyThreshold(data, threshold);

    return imageData;
  }

  /**
   * Converts image data to grayscale using standard luminance formula
   * @param data The image data to process
   */
  private convertToGrayscale(data: Uint8ClampedArray): void {
    for (let i = 0; i < data.length; i += 4) {
      const gray = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
      data[i] = data[i + 1] = data[i + 2] = gray;
    }
  }

  /**
   * Applies a box blur effect to the image data
   * @param data The image data to process
   * @param blurAmount The radius of the blur effect
   */
  private applyBlur(data: Uint8ClampedArray, blurAmount: number): void {
    if (blurAmount === 0) return;

    const tempData = new Uint8ClampedArray(data.length);

    for (let y = 0; y < this.height; y++) {
      for (let x = 0; x < this.width; x++) {
        const result = this.calculateBlurredPixel(data, x, y, blurAmount);
        const i = (y * this.width + x) * 4;

        tempData[i] = result;
        tempData[i + 1] = result;
        tempData[i + 2] = result;
        tempData[i + 3] = data[i + 3];
      }
    }

    for (let i = 0; i < data.length; i++) {
      data[i] = tempData[i];
    }
  }

  /**
   * Calculates the blurred value for a single pixel
   * @param data The image data
   * @param x The x coordinate of the pixel
   * @param y The y coordinate of the pixel
   * @param blurAmount The radius of the blur
   * @returns The blurred pixel value
   */
  private calculateBlurredPixel(
    data: Uint8ClampedArray,
    x: number,
    y: number,
    blurAmount: number
  ): number {
    let sum = 0;
    let count = 0;

    for (let dy = -blurAmount; dy <= blurAmount; dy++) {
      for (let dx = -blurAmount; dx <= blurAmount; dx++) {
        const nx = x + dx;
        const ny = y + dy;

        if (nx >= 0 && nx < this.width && ny >= 0 && ny < this.height) {
          const i = (ny * this.width + nx) * 4;
          sum += data[i];
          count++;
        }
      }
    }

    return sum / count;
  }

  /**
   * Applies threshold to convert grayscale to pure black and white
   * @param data The image data to process
   * @param threshold The threshold value (0-255)
   */
  private applyThreshold(data: Uint8ClampedArray, threshold: number): void {
    for (let i = 0; i < data.length; i += 4) {
      const value = data[i] < threshold ? 0 : 255;
      data[i] = data[i + 1] = data[i + 2] = value;
    }
  }

  /**
   * Calculates image statistics including black pixel count and recommended dot count
   * @param imageData The processed black and white image data
   * @returns ImageStats object containing analysis results
   */
  public calculateImageStats(imageData: ImageData): ImageStats {
    const data = imageData.data;
    const totalPixels = this.width * this.height;
    let blackPixels = 0;

    for (let i = 0; i < data.length; i += 4) {
      if (data[i] === 0) {
        blackPixels++;
      }
    }

    const blackPercentage = blackPixels / totalPixels;
    const recommendedDotCount = Math.ceil(
      blackPercentage * CONFIG.IMAGE.MAX_DOT_COUNT
    );

    return {
      blackPixels,
      totalPixels,
      blackPercentage,
      recommendedDotCount,
    };
  }

  /**
   * Updates the canvas with new image data
   * @param imageData The image data to draw
   */
  public updateCanvas(imageData: ImageData): void {
    this.ctx.putImageData(imageData, 0, 0);
  }

  /**
   * Clears the canvas
   */
  public clearCanvas(): void {
    this.ctx.clearRect(0, 0, this.width, this.height);
  }

  /**
   * Resizes the processor dimensions
   * @param width New width
   * @param height New height
   */
  public resize(width: number, height: number): void {
    this.width = width;
    this.height = height;
  }

  /**
   * Gets the current dimensions of the processor
   * @returns Object containing width and height
   */
  public getDimensions(): { width: number; height: number } {
    return {
      width: this.width,
      height: this.height,
    };
  }
}
