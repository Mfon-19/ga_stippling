cmake_minimum_required(VERSION 3.20)

project(stippling_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
  stippling_engine
  STATIC
  engine/src/c_api.cpp
  engine/src/engine.cpp
  engine/src/optimizer.cpp
  engine/src/raster_grid.cpp
)

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
