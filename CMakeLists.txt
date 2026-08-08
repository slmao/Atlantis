cmake_minimum_required(VERSION 3.21)

project(Atlantis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLANTIS_BUILD_TESTS "Build Atlantis unit tests" ON)
option(ATLANTIS_BUILD_EXAMPLES "Build Atlantis example programs" ON)

include(cmake/CompilerWarnings.cmake)

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/rhi)

if(ATLANTIS_BUILD_EXAMPLES)
  add_subdirectory(examples/foundation_demo)
  add_subdirectory(examples/platform_demo)
endif()

if(ATLANTIS_BUILD_TESTS)
  enable_testing()
  include(cmake/AtlantisDependencies.cmake)
  add_subdirectory(tests/core)
  add_subdirectory(tests/platform)
  add_subdirectory(tests/rhi)
endif()
