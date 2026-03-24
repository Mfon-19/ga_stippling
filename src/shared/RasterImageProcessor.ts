import {
  TargetProcessingConfig,
  TargetStats,
} from "./engineProtocol";
import { CONFIG } from "../utils/config";

export interface PreprocessResult {
  imageData: ImageData;
  stats: TargetStats;
}

/**
 * Pure pixel processor shared by the browser fallback path and the worker
 * backend. This keeps preprocessing logic detached from canvas contexts so it
 * can later move cleanly into the native C++ engine.
 */
export class RasterImageProcessor {
  public preprocess(
    sourceImageData: ImageData,
    config: TargetProcessingConfig
  ): PreprocessResult {
    const imageData = new ImageData(
      new Uint8ClampedArray(sourceImageData.data),
      sourceImageData.width,
      sourceImageData.height
    );
    const data = imageData.data;

    this.convertToGrayscale(data);
    this.applyBlur(data, imageData.width, imageData.height, config.blurAmount);
    this.applyThreshold(data, config.threshold);

    return {
      imageData,
      stats: this.calculateImageStats(
        data,
        imageData.width,
        imageData.height,
        config.maxDotCount
      ),
    };
  }

  private convertToGrayscale(data: Uint8ClampedArray): void {
    for (let i = 0; i < data.length; i += 4) {
      const gray = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
      data[i] = data[i + 1] = data[i + 2] = gray;
    }
  }

  private applyBlur(
    data: Uint8ClampedArray,
    width: number,
    height: number,
    blurAmount: number
  ): void {
    if (blurAmount === 0) {
      return;
    }

    const tempData = new Uint8ClampedArray(data.length);

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const result = this.calculateBlurredPixel(
          data,
          width,
          height,
          x,
          y,
          blurAmount
        );
        const index = (y * width + x) * 4;

        tempData[index] = result;
        tempData[index + 1] = result;
        tempData[index + 2] = result;
        tempData[index + 3] = data[index + 3];
      }
    }

    data.set(tempData);
  }

  private calculateBlurredPixel(
    data: Uint8ClampedArray,
    width: number,
    height: number,
    x: number,
    y: number,
    blurAmount: number
  ): number {
    let sum = 0;
    let count = 0;

    for (let dy = -blurAmount; dy <= blurAmount; dy++) {
      for (let dx = -blurAmount; dx <= blurAmount; dx++) {
        const nextX = x + dx;
        const nextY = y + dy;

        if (nextX >= 0 && nextX < width && nextY >= 0 && nextY < height) {
          const index = (nextY * width + nextX) * 4;
          sum += data[index];
          count++;
        }
      }
    }

    return sum / count;
  }

  private applyThreshold(
    data: Uint8ClampedArray,
    threshold: number
  ): void {
    for (let i = 0; i < data.length; i += 4) {
      const value = data[i] < threshold ? 0 : 255;
      data[i] = data[i + 1] = data[i + 2] = value;
    }
  }

  private calculateImageStats(
    data: Uint8ClampedArray,
    width: number,
    height: number,
    maxDotCount: number
  ): TargetStats {
    const totalPixels = width * height;
    let blackPixels = 0;

    for (let i = 0; i < data.length; i += 4) {
      if (data[i] === 0) {
        blackPixels++;
      }
    }

    const blackPercentage = blackPixels / totalPixels;
    // Recommend one dot for roughly every 50 dark pixels, then cap the result
    // by overall image area so dark portraits do not start with a near-solid fill.
    const densityBasedCount = Math.ceil(
      blackPixels * CONFIG.IMAGE.RECOMMENDED_DOTS_PER_BLACK_PIXEL
    );
    const areaCappedCount = Math.ceil(
      totalPixels * CONFIG.IMAGE.MAX_RECOMMENDED_DOT_PERCENTAGE
    );

    return {
      blackPixels,
      totalPixels,
      blackPercentage,
      recommendedDotCount:
        blackPixels === 0
          ? 0
          : Math.min(maxDotCount, Math.max(1, Math.min(densityBasedCount, areaCappedCount))),
    };
  }
}
