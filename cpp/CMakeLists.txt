cmake_minimum_required(VERSION 3.20)

project(stippling_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Native and WASM must give bit-identical results, so no FMA contraction
# (native builds may fuse a*b+c; WASM can't).
add_compile_options(-ffp-contract=off)

set(
  STIPPLING_ENGINE_SOURCES
  engine/src/c_api.cpp
  engine/src/engine.cpp
  engine/src/export.cpp
  engine/src/optimizer.cpp
  engine/src/raster_grid.cpp
  engine/src/worker_pool.cpp
)

if(EMSCRIPTEN)
  # Two single-file ES modules (so Vite can import them without a separate
  # .wasm asset): a threaded build for cross-origin isolated pages, and a
  # single-threaded fallback. Both produce the same results.
  function(add_stippling_wasm target output_name)
    add_executable(${target} ${STIPPLING_ENGINE_SOURCES})
    target_include_directories(${target} PRIVATE engine/include)
    # Without this, Emscripten can't catch exceptions and every engine error
    # aborts the module. Legacy encoding: Chrome 95+, Firefox 100+, Safari 15.2+.
    target_compile_options(${target} PRIVATE -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=1)
    set_target_properties(
      ${target}
      PROPERTIES
        OUTPUT_NAME "${output_name}"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../src/wasm/generated"
    )
    target_link_options(
      ${target}
      PRIVATE
        "--no-entry"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sSINGLE_FILE=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sFILESYSTEM=0"
        "-sEXPORT_NAME=createStipplingEngineModule"
        "-fwasm-exceptions"
        "-sWASM_LEGACY_EXCEPTIONS=1"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_stippling_engine_create','_stippling_engine_destroy','_stippling_engine_configure','_stippling_engine_prepare_target_rgba8','_stippling_engine_initialize_optimizer','_stippling_engine_evolve_batch','_stippling_engine_prepared_image_width','_stippling_engine_prepared_image_height','_stippling_engine_prepared_image_byte_length','_stippling_engine_copy_prepared_image_rgba8','_stippling_engine_target_black_pixels','_stippling_engine_target_total_pixels','_stippling_engine_target_black_percentage','_stippling_engine_target_recommended_dot_count','_stippling_engine_capture_best_dots','_stippling_engine_best_dots_data','_stippling_engine_best_dots_count','_stippling_engine_export','_stippling_engine_export_data','_stippling_engine_export_size','_stippling_engine_optimizer_generation','_stippling_engine_optimizer_best_fitness','_stippling_engine_last_error']"
        "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','wasmMemory']"
    )
  endfunction()

  add_stippling_wasm(stippling_engine_wasm stipplingEngine)
  target_link_options(stippling_engine_wasm PRIVATE "-sENVIRONMENT=web,worker")

  add_stippling_wasm(stippling_engine_wasm_threads stipplingEngineThreads)
  target_compile_options(stippling_engine_wasm_threads PRIVATE -pthread)
  # Workers are created up front: a thread that has to wait for a new worker to
  # start would deadlock, so running out of pool is an error instead. The
  # engine uses one thread per core (up to 8), and the caller counts as one.
  target_link_options(
    stippling_engine_wasm_threads
    PRIVATE
      "-pthread"
      "-sENVIRONMENT=web,worker,node"
      "-sPTHREAD_POOL_SIZE=Math.min(8,(typeof navigator!='undefined'&&navigator.hardwareConcurrency)||4)-1"
      "-sPTHREAD_POOL_SIZE_STRICT=2"
  )
  return()
endif()

find_package(Threads REQUIRED)

add_library(stippling_engine STATIC ${STIPPLING_ENGINE_SOURCES})
target_link_libraries(stippling_engine PUBLIC Threads::Threads)

target_include_directories(
  stippling_engine
  PUBLIC
  engine/include
)

add_executable(stippling_cli cli/main.cpp)
target_link_libraries(stippling_cli PRIVATE stippling_engine)

add_executable(stippling_raster_bench bench/raster_bench.cpp)
target_link_libraries(stippling_raster_bench PRIVATE stippling_engine)

enable_testing()

add_executable(stippling_smoke_test tests/smoke_test.cpp)
target_link_libraries(stippling_smoke_test PRIVATE stippling_engine)

add_executable(stippling_raster_grid_test tests/raster_grid_test.cpp)
target_link_libraries(stippling_raster_grid_test PRIVATE stippling_engine)

add_executable(stippling_c_api_test tests/c_api_test.cpp)
target_link_libraries(stippling_c_api_test PRIVATE stippling_engine)

add_executable(stippling_optimizer_test tests/optimizer_test.cpp)
target_link_libraries(stippling_optimizer_test PRIVATE stippling_engine)

add_executable(stippling_export_validation_test tests/export_validation_test.cpp)
target_link_libraries(stippling_export_validation_test PRIVATE stippling_engine)

add_test(NAME stippling_smoke_test COMMAND stippling_smoke_test)
add_test(NAME stippling_raster_grid_test COMMAND stippling_raster_grid_test)
add_test(NAME stippling_c_api_test COMMAND stippling_c_api_test)
add_test(NAME stippling_optimizer_test COMMAND stippling_optimizer_test)
add_test(NAME stippling_export_validation_test COMMAND stippling_export_validation_test)
