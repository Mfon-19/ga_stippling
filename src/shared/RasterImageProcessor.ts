import { TargetProcessingConfig, TargetStats } from "./engineProtocol";
import { CONFIG } from "../utils/config";

export interface PreprocessResult {
  imageData: ImageData;
  stats: TargetStats;
}

/**
 * Pure pixel processor shared by the browser fallback path and benchmark tools.
 * It mirrors the native engine's target preparation so the TypeScript baseline
 * and the WASM path are compared against the same processed target.
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
    const grayscale = this.extractGrayscaleChannel(imageData.data);
    const blurred = this.applySeparableBlur(
      grayscale,
      imageData.width,
      imageData.height,
      config.blurAmount
    );
    const edges = this.computeEdgeResponse(
      blurred,
      imageData.width,
      imageData.height
    );
    const structure = this.computeLocalStructure(grayscale, blurred);
    const importance = this.combineImportance(blurred, edges, structure);
    const thresholded = this.applyThreshold(
      this.quantizeChannel(blurred),
      config.threshold
    );

    this.writeChannelToImageData(imageData.data, thresholded);

    return {
      imageData,
      stats: this.calculateImageStats(
        thresholded,
        importance,
        imageData.width,
        imageData.height,
        config.maxDotCount
      ),
    };
  }

  private extractGrayscaleChannel(data: Uint8ClampedArray): Float64Array {
    const grayscale = new Float64Array(data.length / 4);

    for (let index = 0; index < grayscale.length; index += 1) {
      const pixelIndex = index * 4;
      grayscale[index] =
        0.299 * data[pixelIndex] +
        0.587 * data[pixelIndex + 1] +
        0.114 * data[pixelIndex + 2];
    }

    return grayscale;
  }

  private applySeparableBlur(
    source: Float64Array,
    width: number,
    height: number,
    blurAmount: number
  ): Float64Array {
    if (blurAmount === 0) {
      return source.slice();
    }

    const kernel = this.buildGaussianKernel(blurAmount);
    const radius = (kernel.length - 1) / 2;
    const horizontal = new Float64Array(source.length);
    const blurred = new Float64Array(source.length);

    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        let value = 0;
        for (let offset = -radius; offset <= radius; offset += 1) {
          const sampleX = Math.min(width - 1, Math.max(0, x + offset));
          value += source[y * width + sampleX] * kernel[offset + radius];
        }
        horizontal[y * width + x] = value;
      }
    }

    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        let value = 0;
        for (let offset = -radius; offset <= radius; offset += 1) {
          const sampleY = Math.min(height - 1, Math.max(0, y + offset));
          value += horizontal[sampleY * width + x] * kernel[offset + radius];
        }
        blurred[y * width + x] = value;
      }
    }

    return blurred;
  }

  private buildGaussianKernel(blurAmount: number): number[] {
    if (blurAmount === 0) {
      return [1];
    }

    const sigma = Math.max(0.85, blurAmount * 0.65);
    const radius = Math.max(1, Math.ceil(sigma * 2));
    const kernel: number[] = [];
    let weightSum = 0;

    for (let offset = -radius; offset <= radius; offset += 1) {
      const weight = Math.exp(-(offset * offset) / (2 * sigma * sigma));
      kernel.push(weight);
      weightSum += weight;
    }

    return kernel.map((weight) => weight / weightSum);
  }

  private computeEdgeResponse(
    grayscale: Float64Array,
    width: number,
    height: number
  ): Float64Array {
    const edges = new Float64Array(grayscale.length);
    const sample = (x: number, y: number) =>
      grayscale[Math.min(height - 1, Math.max(0, y)) * width + Math.min(width - 1, Math.max(0, x))];

    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const gx =
          -sample(x - 1, y - 1) +
          sample(x + 1, y - 1) -
          2 * sample(x - 1, y) +
          2 * sample(x + 1, y) -
          sample(x - 1, y + 1) +
          sample(x + 1, y + 1);
        const gy =
          sample(x - 1, y - 1) +
          2 * sample(x, y - 1) +
          sample(x + 1, y - 1) -
          sample(x - 1, y + 1) -
          2 * sample(x, y + 1) -
          sample(x + 1, y + 1);
        edges[y * width + x] = Math.min(
          1,
          Math.sqrt(gx * gx + gy * gy) / 1020
        );
      }
    }

    return edges;
  }

  private computeLocalStructure(
    grayscale: Float64Array,
    blurred: Float64Array
  ): Float64Array {
    const structure = new Float64Array(grayscale.length);

    for (let index = 0; index < grayscale.length; index += 1) {
      structure[index] = Math.min(
        1,
        Math.abs(grayscale[index] - blurred[index]) / 255
      );
    }

    return structure;
  }

  private combineImportance(
    grayscale: Float64Array,
    edges: Float64Array,
    structure: Float64Array
  ): Float64Array {
    const importance = new Float64Array(grayscale.length);

    for (let index = 0; index < grayscale.length; index += 1) {
      const darkness = Math.min(1, Math.max(0, (255 - grayscale[index]) / 255));
      let combined =
        0.55 * darkness + 0.3 * edges[index] + 0.15 * structure[index];

      if (darkness > 0.1) {
        combined = Math.max(combined, darkness * 0.35);
      }

      importance[index] = Math.min(1, Math.max(0, combined));
    }

    return importance;
  }

  private quantizeChannel(source: Float64Array): Uint8ClampedArray {
    const quantized = new Uint8ClampedArray(source.length);
    for (let index = 0; index < source.length; index += 1) {
      quantized[index] = Math.min(255, Math.max(0, Math.round(source[index])));
    }
    return quantized;
  }

  private applyThreshold(
    grayscale: Uint8ClampedArray,
    threshold: number
  ): Uint8ClampedArray {
    const thresholded = new Uint8ClampedArray(grayscale.length);
    for (let index = 0; index < grayscale.length; index += 1) {
      thresholded[index] = grayscale[index] < threshold ? 0 : 255;
    }
    return thresholded;
  }

  private writeChannelToImageData(
    data: Uint8ClampedArray,
    channel: Uint8ClampedArray
  ): void {
    for (let index = 0; index < channel.length; index += 1) {
      const pixelIndex = index * 4;
      data[pixelIndex] = channel[index];
      data[pixelIndex + 1] = channel[index];
      data[pixelIndex + 2] = channel[index];
    }
  }

  private calculateImageStats(
    thresholded: Uint8ClampedArray,
    importance: Float64Array,
    width: number,
    height: number,
    maxDotCount: number
  ): TargetStats {
    const totalPixels = width * height;
    let blackPixels = 0;
    let totalImportance = 0;

    for (let index = 0; index < thresholded.length; index += 1) {
      if (thresholded[index] === 0) {
        blackPixels += 1;
      }
      totalImportance += importance[index];
    }

    const blackPercentage = totalPixels === 0 ? 0 : blackPixels / totalPixels;
    const importanceBasedCount = Math.ceil(
      totalImportance * CONFIG.IMAGE.RECOMMENDED_DOTS_PER_IMPORTANCE_POINT
    );
    const areaCappedCount = Math.ceil(
      totalPixels * CONFIG.IMAGE.MAX_RECOMMENDED_DOT_PERCENTAGE
    );

    return {
      blackPixels,
      totalPixels,
      blackPercentage,
      recommendedDotCount:
        totalImportance <= 0 && blackPixels === 0
          ? 0
          : Math.min(
              maxDotCount,
              Math.max(
                1,
                Math.min(
                  areaCappedCount,
                  Math.max(importanceBasedCount, Math.ceil(blackPixels * 0.006))
                )
              )
            ),
    };
  }
}
