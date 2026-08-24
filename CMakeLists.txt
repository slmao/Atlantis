cmake_minimum_required(VERSION 3.21)

project(Atlantis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLANTIS_BUILD_TESTS "Build Atlantis unit tests" ON)
option(ATLANTIS_BUILD_EXAMPLES "Build Atlantis example programs" ON)

include(cmake/CompilerWarnings.cmake)
# Unconditional, independent of ATLANTIS_BUILD_TESTS -- src/tools/asset_cooker
# (added unconditionally below) needs Stb::Stb too (Spec 0016). Must
# precede add_subdirectory(src/tools/asset_cooker).
include(cmake/AtlantisStb.cmake)

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
# Plan 0014 Section D1: the earliest point both of Atlantis::World's own
# dependencies (Atlantis::Core, Atlantis::AssetSystem) are already
# declared.
add_subdirectory(src/world)
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
add_subdirectory(src/tools/asset_cooker)
# Must come after add_subdirectory(src/asset_system) and
# add_subdirectory(src/tools/asset_cooker) (Plan 0012 Section D4):
# atlantis_add_static_mesh_asset() is defined in the former and this
# call site's own custom command invokes the atlantis_asset_cooker
# target defined in the latter.
add_subdirectory(assets)
# Plan 0012 Section D4: called exactly once, after every
# add_subdirectory() that could declare an asset (today, just assets/
# above) -- writes the accumulated declared-asset list and declares the
# set-level validation target.
atlantis_finalize_asset_validation()
# Must come after add_subdirectory(src/shader_system) (Plan 0008 Section
# 7/8): atlantis_add_slang_shader_pair() is defined there, and this call
# site invokes it.
add_subdirectory(shaders/minimal_renderer)
# Plan 0013 Section D1: must come after both add_subdirectory(assets)
# and add_subdirectory(shaders/minimal_renderer) above -- atlantis_runtime
# add_dependencies() on minimal_mesh_shaders and
# ${ATLANTIS_minimal_cube_TARGET}, both declared by those two calls, and
# CMake's add_dependencies() requires the named target to already be
# declared at the point the call is parsed. Runtime is a product
# executable, not an example, so it is unconditional here, not inside
# the ATLANTIS_BUILD_EXAMPLES block below.
add_subdirectory(src/runtime)

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
  add_subdirectory(tests/world)
  add_subdirectory(tests/platform)
  add_subdirectory(tests/rhi)
  add_subdirectory(tests/vulkan_backend)
  # Plan 0016 Milestone 9 (D11): test-only shader pair, unlike
  # shaders/minimal_renderer's own unconditional placement above --
  # only tests/image_regression/fixture/ (added next) consumes it.
  add_subdirectory(shaders/textured_quad)
  add_subdirectory(tests/image_regression)
  add_subdirectory(tests/render_graph)
  add_subdirectory(tests/renderer)
  add_subdirectory(tests/shader_system)
  add_subdirectory(tests/tools/shader_compiler)
  add_subdirectory(tests/tools/asset_cooker)
  add_subdirectory(tests/runtime)
endif()
