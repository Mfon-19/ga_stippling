import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

export class NodeImageData {
  public readonly data: Uint8ClampedArray;
  public readonly width: number;
  public readonly height: number;

  constructor(data: Uint8ClampedArray, width: number, height: number) {
    if (data.length !== width * height * 4) {
      throw new Error(
        `ImageData length ${data.length} does not match ${width}x${height}`
      );
    }

    this.data = data;
    this.width = width;
    this.height = height;
  }
}

export interface DecodedFixture {
  name: string;
  imagePath: string;
  imageData: NodeImageData;
}

export function installNodeImageData(): void {
  const globalScope = globalThis as typeof globalThis & {
    ImageData?: typeof NodeImageData;
  };

  if (!globalScope.ImageData) {
    globalScope.ImageData = NodeImageData;
  }
}

function nextToken(source: Buffer, state: { offset: number }): string {
  while (state.offset < source.length) {
    const byte = source[state.offset];
    if (byte === 35) {
      while (state.offset < source.length && source[state.offset] !== 10) {
        state.offset += 1;
      }
      continue;
    }
    if (byte === 9 || byte === 10 || byte === 13 || byte === 32) {
      state.offset += 1;
      continue;
    }
    break;
  }

  const start = state.offset;
  while (state.offset < source.length) {
    const byte = source[state.offset];
    if (byte === 9 || byte === 10 || byte === 13 || byte === 32) {
      break;
    }
    state.offset += 1;
  }

  if (start === state.offset) {
    throw new Error("Unexpected end of Netpbm header");
  }

  return source.toString("ascii", start, state.offset);
}

function decodeNetpbmImage(imagePath: string): DecodedFixture {
  const source = readFileSync(imagePath);
  const state = { offset: 0 };
  const magic = nextToken(source, state);
  const width = Number.parseInt(nextToken(source, state), 10);
  const height = Number.parseInt(nextToken(source, state), 10);
  const maxValue = Number.parseInt(nextToken(source, state), 10);
  if (maxValue !== 255) {
    throw new Error(`Unsupported Netpbm max value ${maxValue}`);
  }

  while (
    state.offset < source.length &&
    [9, 10, 13, 32].includes(source[state.offset])
  ) {
    state.offset += 1;
  }

  const pixelCount = width * height;
  const rgba = new Uint8ClampedArray(pixelCount * 4);
  for (let index = 0; index < pixelCount; index += 1) {
    rgba[index * 4 + 3] = 255;
  }

  if (magic === "P2") {
    for (let index = 0; index < pixelCount; index += 1) {
      const value = Number.parseInt(nextToken(source, state), 10);
      rgba[index * 4] = value;
      rgba[index * 4 + 1] = value;
      rgba[index * 4 + 2] = value;
    }
  } else if (magic === "P3") {
    for (let index = 0; index < pixelCount; index += 1) {
      rgba[index * 4] = Number.parseInt(nextToken(source, state), 10);
      rgba[index * 4 + 1] = Number.parseInt(nextToken(source, state), 10);
      rgba[index * 4 + 2] = Number.parseInt(nextToken(source, state), 10);
    }
  } else if (magic === "P5") {
    for (let index = 0; index < pixelCount; index += 1) {
      const value = source[state.offset + index];
      rgba[index * 4] = value;
      rgba[index * 4 + 1] = value;
      rgba[index * 4 + 2] = value;
    }
  } else if (magic === "P6") {
    for (let index = 0; index < pixelCount; index += 1) {
      rgba[index * 4] = source[state.offset + index * 3];
      rgba[index * 4 + 1] = source[state.offset + index * 3 + 1];
      rgba[index * 4 + 2] = source[state.offset + index * 3 + 2];
    }
  } else {
    throw new Error(`Unsupported Netpbm format ${magic}`);
  }

  return {
    name: path.basename(imagePath),
    imagePath,
    imageData: new NodeImageData(rgba, width, height),
  };
}

function decodeWithSwift(imagePath: string): DecodedFixture {
  const repoRoot = process.cwd();
  const tempDirectory = mkdtempSync(path.join(tmpdir(), "stippling-benchmark-"));
  const outputPath = path.join(tempDirectory, `${path.basename(imagePath)}.rgba`);

  try {
    const stdout = execFileSync(
      "swift",
      [path.join(repoRoot, "scripts", "decode-image.swift"), imagePath, outputPath],
      {
        cwd: repoRoot,
        encoding: "utf8",
      }
    );
    const payload = JSON.parse(stdout) as {
      width: number;
      height: number;
      output: string;
    };
    const pixels = Uint8ClampedArray.from(readFileSync(payload.output));

    return {
      name: path.basename(imagePath),
      imagePath,
      imageData: new NodeImageData(pixels, payload.width, payload.height),
    };
  } finally {
    rmSync(tempDirectory, { recursive: true, force: true });
  }
}

export function decodeImageFile(imagePath: string): DecodedFixture {
  const extension = path.extname(imagePath).toLowerCase();
  if (extension === ".pgm" || extension === ".ppm" || extension === ".pnm") {
    return decodeNetpbmImage(imagePath);
  }

  return decodeWithSwift(imagePath);
}
