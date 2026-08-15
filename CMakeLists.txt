cmake_minimum_required(VERSION 3.21)

project(Atlantis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLANTIS_BUILD_TESTS "Build Atlantis unit tests" ON)
option(ATLANTIS_BUILD_EXAMPLES "Build Atlantis example programs" ON)

include(cmake/CompilerWarnings.cmake)

find_package(Vulkan REQUIRED)

add_subdirectory(src/core)
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

if(ATLANTIS_BUILD_EXAMPLES)
  add_subdirectory(examples/foundation_demo)
  add_subdirectory(examples/platform_demo)
  add_subdirectory(examples/rhi_vulkan_demo)
  add_subdirectory(examples/frame_execution_demo)
  add_subdirectory(examples/minimal_renderer_demo)
endif()

if(ATLANTIS_BUILD_TESTS)
  enable_testing()
  include(cmake/AtlantisDependencies.cmake)
  add_subdirectory(tests/core)
  add_subdirectory(tests/platform)
  add_subdirectory(tests/rhi)
  add_subdirectory(tests/vulkan_backend)
  add_subdirectory(tests/render_graph)
  add_subdirectory(tests/renderer)
  add_subdirectory(tests/shader_system)
endif()
