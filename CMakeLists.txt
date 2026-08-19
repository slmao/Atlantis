cmake_minimum_required(VERSION 3.21)

project(Atlantis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLANTIS_BUILD_TESTS "Build Atlantis unit tests" ON)
option(ATLANTIS_BUILD_EXAMPLES "Build Atlantis example programs" ON)

include(cmake/CompilerWarnings.cmake)

find_package(Vulkan REQUIRED)

# Plan 0008 Section 7: slangc/spirv-val are newly-required build tools,
# sourced from the existing Vulkan SDK requirement -- no new dependency-
# acquisition mechanism. A missing tool fails CMake configure, not build
# or runtime; spirv-val is mandatory (ADR-0031), not optional.
find_program(ATLANTIS_SLANGC_EXECUTABLE
  NAMES slangc
  HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(NOT ATLANTIS_SLANGC_EXECUTABLE)
  message(FATAL_ERROR
    "slangc not found. It ships with the Vulkan SDK (confirmed present "
    "since SDK 1.3.296.0) -- verify VULKAN_SDK is set and points at an "
    "SDK install whose Bin/ directory contains slangc.exe.")
endif()

find_program(ATLANTIS_SPIRV_VAL_EXECUTABLE
  NAMES spirv-val
  HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(NOT ATLANTIS_SPIRV_VAL_EXECUTABLE)
  message(FATAL_ERROR
    "spirv-val not found. It ships with the Vulkan SDK alongside slangc "
    "-- verify VULKAN_SDK is set and points at an SDK install whose "
    "Bin/ directory contains spirv-val.exe. spirv-val is a MANDATORY "
    "build-time verification step (ADR-0031), not optional.")
endif()

add_subdirectory(src/core)
add_subdirectory(src/asset_system)
add_subdirectory(src/platform)
add_subdirectory(src/rhi)
add_subdirectory(src/vulkan_backend)
add_subdirectory(src/render_graph)
add_subdirectory(src/renderer)
# Must precede any add_subdirectory() that calls
# atlantis_add_slang_shader_pair() (Plan 0008 Section 7) -- that CMake
# function is defined inside src/shader_system/CMakeLists.txt itself,
# and only directories processed after this one can see it.
add_subdirectory(src/shader_system)
add_subdirectory(src/tools/shader_compiler)
# Must come after add_subdirectory(src/shader_system) (Plan 0008 Section
# 7/8): atlantis_add_slang_shader_pair() is defined there, and this call
# site invokes it.
add_subdirectory(shaders/minimal_renderer)

if(ATLANTIS_BUILD_EXAMPLES)
  add_subdirectory(examples/foundation_demo)
  add_subdirectory(examples/platform_demo)
  add_subdirectory(examples/rhi_vulkan_demo)
  add_subdirectory(examples/frame_execution_demo)
  add_subdirectory(examples/minimal_renderer_demo)
  add_subdirectory(examples/headless_rendering_demo)
endif()

if(ATLANTIS_BUILD_TESTS)
  enable_testing()
  include(cmake/AtlantisDependencies.cmake)
  add_subdirectory(tests/core)
  add_subdirectory(tests/asset_system)
  add_subdirectory(tests/platform)
  add_subdirectory(tests/rhi)
  add_subdirectory(tests/vulkan_backend)
  add_subdirectory(tests/image_regression)
  add_subdirectory(tests/render_graph)
  add_subdirectory(tests/renderer)
  add_subdirectory(tests/shader_system)
  add_subdirectory(tests/tools/shader_compiler)
endif()
