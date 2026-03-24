import { EngineArtifactEvent, SerializedDot } from "./engineProtocol";

export interface TimelapseFrame {
  generation: number;
  dots: SerializedDot[];
}

function formatCircle(dot: SerializedDot, scale: number): string {
  return `<circle cx="${dot.x * scale}" cy="${dot.y * scale}" r="${dot.radius * scale}" fill="black" />`;
}

export function renderTimelapseSvg(
  frames: TimelapseFrame[],
  width: number,
  height: number,
  scale: number,
  frameDurationMs: number
): string {
  if (frames.length === 0) {
    throw new Error("Timelapse export requires at least one captured frame");
  }

  const resolvedScale = Math.max(1, Math.floor(scale));
  const resolvedFrameDuration = Math.max(1, Math.floor(frameDurationMs));
  const totalDuration = frames.length * resolvedFrameDuration;
  const body = frames
    .map((frame, index) => {
      const startMs = index * resolvedFrameDuration;
      const circles = frame.dots.map((dot) => formatCircle(dot, resolvedScale)).join("");
      return [
        `<g data-generation="${frame.generation}" opacity="0">`,
        `<set attributeName="opacity" to="1" begin="${startMs}ms" dur="${resolvedFrameDuration}ms" repeatCount="indefinite" />`,
        `<set attributeName="opacity" to="1" begin="${totalDuration}ms" dur="${resolvedFrameDuration}ms" repeatCount="indefinite" />`,
        circles,
        "</g>",
      ].join("");
    })
    .join("");

  return [
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width * resolvedScale} ${height * resolvedScale}" width="${width * resolvedScale}" height="${height * resolvedScale}">`,
    '<rect width="100%" height="100%" fill="white" />',
    body,
    "</svg>",
  ].join("");
}

export function createTextArtifact(
  requestId: string,
  runId: string,
  format: EngineArtifactEvent["format"],
  mimeType: string,
  filename: string,
  contents: string
): EngineArtifactEvent {
  const encoded = new TextEncoder().encode(contents);
  return {
    type: "artifact",
    requestId,
    runId,
    format,
    mimeType,
    filename,
    data: encoded.buffer,
  };
}
