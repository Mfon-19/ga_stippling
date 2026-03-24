cmake_minimum_required(VERSION 3.20)

project(stippling_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(
  STIPPLING_ENGINE_SOURCES
  engine/src/c_api.cpp
  engine/src/engine.cpp
  engine/src/optimizer.cpp
  engine/src/raster_grid.cpp
)

if(EMSCRIPTEN)
  # The browser worker consumes a single-file ES module so Vite can import the
  # generated runtime without managing a separate wasm asset path.
  add_executable(stippling_engine_wasm ${STIPPLING_ENGINE_SOURCES})
  target_include_directories(stippling_engine_wasm PRIVATE engine/include)
  set_target_properties(
    stippling_engine_wasm
    PROPERTIES
      OUTPUT_NAME "stipplingEngine"
      SUFFIX ".js"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../src/wasm/generated"
  )
  target_link_options(
    stippling_engine_wasm
    PRIVATE
      "--no-entry"
      "-sMODULARIZE=1"
      "-sEXPORT_ES6=1"
      "-sSINGLE_FILE=1"
      "-sALLOW_MEMORY_GROWTH=1"
      "-sFILESYSTEM=0"
      "-sENVIRONMENT=web,worker"
      "-sEXPORT_NAME=createStipplingEngineModule"
      "-sEXPORTED_FUNCTIONS=['_malloc','_free','_stippling_engine_create','_stippling_engine_destroy','_stippling_engine_configure_values','_stippling_engine_prepare_target_rgba8','_stippling_engine_initialize_optimizer','_stippling_engine_evolve_batch_in_place','_stippling_engine_prepared_image_width','_stippling_engine_prepared_image_height','_stippling_engine_prepared_image_byte_length','_stippling_engine_copy_prepared_image_rgba8','_stippling_engine_target_black_pixels','_stippling_engine_target_total_pixels','_stippling_engine_target_black_percentage','_stippling_engine_target_recommended_dot_count','_stippling_engine_best_dot_count','_stippling_engine_copy_best_dots','_stippling_engine_optimizer_generation','_stippling_engine_optimizer_best_fitness','_stippling_engine_last_error']"
      "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString']"
  )
  return()
endif()

add_library(stippling_engine STATIC ${STIPPLING_ENGINE_SOURCES})

target_include_directories(
  stippling_engine
  PUBLIC
  engine/include
)

add_executable(stippling_cli cli/main.cpp)
target_link_libraries(stippling_cli PRIVATE stippling_engine)

enable_testing()

add_executable(stippling_smoke_test tests/smoke_test.cpp)
target_link_libraries(stippling_smoke_test PRIVATE stippling_engine)

add_executable(stippling_raster_grid_test tests/raster_grid_test.cpp)
target_link_libraries(stippling_raster_grid_test PRIVATE stippling_engine)

add_executable(stippling_c_api_test tests/c_api_test.cpp)
target_link_libraries(stippling_c_api_test PRIVATE stippling_engine)

add_executable(stippling_optimizer_test tests/optimizer_test.cpp)
target_link_libraries(stippling_optimizer_test PRIVATE stippling_engine)

add_test(NAME stippling_smoke_test COMMAND stippling_smoke_test)
add_test(NAME stippling_raster_grid_test COMMAND stippling_raster_grid_test)
add_test(NAME stippling_c_api_test COMMAND stippling_c_api_test)
add_test(NAME stippling_optimizer_test COMMAND stippling_optimizer_test)
