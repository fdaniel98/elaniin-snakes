cmake_minimum_required(VERSION 3.25)
project(battlesnake-royale LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(BSR_BUILD_TESTS "Compilar los tests de Catch2" ON)
option(BSR_BUILD_BENCH "Compilar los benchmarks de Google Benchmark" OFF)

# Avisos como errores en todos los presets. Las cabeceras de terceros entran
# como SYSTEM, asi que sus avisos no rompen el build.
add_library(bsr_warnings INTERFACE)
target_compile_options(bsr_warnings INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Wcast-align
  -Wunused
  -Woverloaded-virtual
  -Wnull-dereference
  -Wdouble-promotion
  -Werror
)

add_subdirectory(engine)
add_subdirectory(snake)
add_subdirectory(arena)

if(BSR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BSR_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# Las herramientas acompañan a los tests: comparten el arnes de replay y ninguna entra al
# deploy. Sin tests no hay arnes, asi que no hay herramientas.
if(BSR_BUILD_TESTS)
  add_subdirectory(tools)
endif()
