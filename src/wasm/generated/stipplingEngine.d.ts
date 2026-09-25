export interface GeneratedStipplingEngineModule {
  wasmMemory: WebAssembly.Memory;
  _malloc(size: number): number;
  _free(pointer: number): void;
  UTF8ToString(pointer: number): string;
  _stippling_engine_create(): number;
  _stippling_engine_destroy(enginePointer: number): void;
  _stippling_engine_configure(
    enginePointer: number,
    populationSize: number,
    mutationRate: number,
    dotCount: number,
    elitismRatio: number,
    seed: number,
    generationsPerBatch: number,
    threadCount: number
  ): number;
  _stippling_engine_prepare_target_rgba8(
    enginePointer: number,
    width: number,
    height: number,
    pixelsPointer: number,
    length: number,
    blurAmount: number,
    threshold: number,
    maxDotCount: number
  ): number;
  _stippling_engine_initialize_optimizer(enginePointer: number): number;
  _stippling_engine_evolve_batch(enginePointer: number): number;
  _stippling_engine_prepared_image_width(enginePointer: number): number;
  _stippling_engine_prepared_image_height(enginePointer: number): number;
  _stippling_engine_prepared_image_byte_length(enginePointer: number): number;
  _stippling_engine_copy_prepared_image_rgba8(
    enginePointer: number,
    outputPointer: number,
    capacity: number
  ): number;
  _stippling_engine_target_black_pixels(enginePointer: number): number;
  _stippling_engine_target_total_pixels(enginePointer: number): number;
  _stippling_engine_target_black_percentage(enginePointer: number): number;
  _stippling_engine_target_recommended_dot_count(enginePointer: number): number;
  _stippling_engine_capture_best_dots(enginePointer: number): number;
  _stippling_engine_best_dots_data(enginePointer: number): number;
  _stippling_engine_best_dots_count(enginePointer: number): number;
  _stippling_engine_export(
    enginePointer: number,
    format: number,
    scale: number,
    frameDurationMs: number
  ): number;
  _stippling_engine_export_data(enginePointer: number): number;
  _stippling_engine_export_size(enginePointer: number): number;
  _stippling_engine_optimizer_generation(enginePointer: number): number;
  _stippling_engine_optimizer_best_fitness(enginePointer: number): number;
  _stippling_engine_last_error(enginePointer: number): number;
}

declare function createStipplingEngineModule(): Promise<GeneratedStipplingEngineModule>;

export default createStipplingEngineModule;
