# Plan 0034 Milestone 6 (real build finding, e9207c2): raised from 3.21 --
# cmake/AtlantisStb.cmake's own DOWNLOAD_EXTRACT_TIMESTAMP
# (FetchContent_Declare(), line 24) requires CMake >= 3.24; empirically
# confirmed a real CMake 3.22.1 (the Android SDK's own bundled version)
# silently mis-parses it as a continuation of the preceding URL_HASH
# value instead of a separate keyword, rather than raising a clear
# "unknown argument" error. 3.21 was never actually sufficient for this
# repository's own CMakeLists.txt tree; every build before Milestone 6's
# own diagnosis happened to use a newer CMake already (Windows via
# Visual Studio's bundled 4.3.1; every direct, non-Gradle Android build
# in this Plan), masking the gap. Raising this removes this repository's
# only remaining implicit dependence on a machine-local cmake.dir
# override (android/local.properties, gitignored) to avoid the same
# failure under Gradle's own externalNativeBuild.
cmake_minimum_required(VERSION 3.24)

# Plan 0034 Milestone 2/3: C is needed solely to compile the Android
# NDK's own unmodified android_native_app_glue.c (a real C source file,
# not C++-compatible as-is -- confirmed by a real Android build attempt)
# as the C it actually is, rather than forcing it through a C++
# compiler. Zero-cost on every other configuration: MSVC's cl.exe
# already handles both C and C++ with no separate toolchain to install,
# and Clang (Android's own compiler) does too -- this adds no new
# compiler requirement anywhere this repository builds.
project(Atlantis LANGUAGES CXX C)

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
# Plan 0034 Milestone 1 (ADR-0078/ADR-0080): shader_compiler/asset_cooker
# are host-only build-time tools (Plan 0008 Section 4: "SPIR-V
# compilation always happens on a host, never the Android target
# device") and assets/ below cooks every checked-in authoring asset
# through them. Per ADR-0080, an Android build packages the
# already-cooked artifacts a prior Windows build produced into the
# APK; it never re-cooks or re-compiles them itself, so none of this
# needs to build for the Android target. This gating is a Plan 0034
# deviation from its own Files/Modules Touched list (which named only
# src/platform/, src/vulkan_backend/, src/runtime/, and android/) --
# reported explicitly in the implementing PR, per AGENTS.md.
if(NOT ANDROID)
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
  # above) -- writes the accumulated declared-asset list and declares
  # the set-level validation target. Moved inside this same
  # if(NOT ANDROID) block (Plan 0034 Milestone 1 build-smoke-test
  # finding): atlantis_finalize_asset_validation()'s own implementation
  # (src/asset_system/CMakeLists.txt) unconditionally declares an ALL
  # target that DEPENDS the host tool atlantis_asset_cooker and runs it
  # at build time (Plan 0012 D4/ADR-0044) -- it is itself host-pipeline
  # machinery, not merely "validates an empty set," so it cannot stay
  # unconditional once atlantis_asset_cooker above is gated. On Android,
  # assets/ above declares nothing, so there is no validation target to
  # produce -- confirmed this function has exactly one call site in the
  # whole repository, so nothing else depends on it running unconditionally.
  atlantis_finalize_asset_validation()
endif()
# Plan 0034 Milestone 1 (ADR-0078/ADR-0080): every production shader
# pair below is compiled by atlantis_add_slang_shader_pair()
# (src/shader_system/CMakeLists.txt), itself a host-side slangc/
# spirv-val invocation -- same host-only/cooked-artifact reasoning as
# the src/tools/ and assets/ gate above; same Plan 0034 deviation.
# Must come after add_subdirectory(src/shader_system) (Plan 0008 Section
# 7/8): atlantis_add_slang_shader_pair() is defined there, and this call
# site invokes it.
if(NOT ANDROID)
add_subdirectory(shaders/minimal_renderer)
# Plan 0018 Section P10 / D3: promoted from a test-only fixture resource
# (formerly added inside the ATLANTIS_BUILD_TESTS block below) to a
# production built-in shader backing MaterialKind::UnlitTextured --
# unconditional here, mirroring shaders/minimal_renderer's own placement
# and the identical reason (atlantis_runtime needs it too). No .slang
# source content changes; tests/image_regression/fixture/ (added later,
# inside ATLANTIS_BUILD_TESTS) keeps consuming the exact same target,
# now simply available unconditionally instead of only under
# ATLANTIS_BUILD_TESTS.
add_subdirectory(shaders/textured_quad)
# Plan 0019 Section P11/D8: the lit_textured shader pair backing
# MaterialKind::LitTextured -- unconditional here, matching
# shaders/textured_quad's own identical placement/reasoning; declared
# immediately after shaders/textured_quad and before src/runtime, per
# Spec 0019 D8's own exact placement instruction.
add_subdirectory(shaders/lit_textured)
# Plan 0023 Milestone 4: the pbr_direct_lit shader pair backing
# MaterialKind::PbrDirectLit -- unconditional here, matching
# shaders/lit_textured's own identical placement/reasoning; declared
# immediately after shaders/lit_textured and before src/runtime, for
# the same add_dependencies()-ordering reason documented below.
add_subdirectory(shaders/pbr_direct_lit)
# Plan 0025 Milestone 5: opt-in IBL sibling for PbrDirectLit. The
# existing shader above remains the no-environment path unchanged.
add_subdirectory(shaders/pbr_ibl)
# Plan 0029 Section P12 (ADR-0074 Section 5): the two normal-map shader
# variants -- unconditional here, matching every other production
# shader pair's own identical placement/reasoning; declared immediately
# after the existing PBR variants and before src/runtime, for the same
# add_dependencies()-ordering reason documented below.
add_subdirectory(shaders/pbr_direct_lit_normal_map)
add_subdirectory(shaders/pbr_ibl_normal_map)
# Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own two IBL-lit
# shader pairs -- unconditional here, matching every other production
# shader pair's own identical placement/reasoning; declared immediately
# after the existing PBR normal-map variants and before src/runtime,
# for the same add_dependencies()-ordering reason documented below.
add_subdirectory(shaders/pbr_clearcoat_ibl)
add_subdirectory(shaders/pbr_clearcoat_ibl_normal_map)
# Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own two IBL-lit shader
# pairs -- same shape/placement as PbrClearcoat's own pair immediately
# above.
add_subdirectory(shaders/pbr_sheen_ibl)
add_subdirectory(shaders/pbr_sheen_ibl_normal_map)
# Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own two IBL-lit
# shader pairs -- same shape/placement as PbrClearcoat's/PbrSheen's own
# pairs immediately above.
add_subdirectory(shaders/pbr_anisotropic_ibl)
add_subdirectory(shaders/pbr_anisotropic_ibl_normal_map)
# Plan 0024 Milestone 4 (ADR-0068 D-6/D-10): the two output-transform
# shader pairs -- unconditional here, matching every other production
# shader pair's own identical placement/reasoning; declared immediately
# after the PBR shader variants and before src/runtime, for the same
# add_dependencies()-ordering reason documented below (wired into
# atlantis_runtime's own add_dependencies() in Plan 0024 Milestone 6).
add_subdirectory(shaders/output_transform_unorm)
add_subdirectory(shaders/output_transform_srgb)
# Plan 0026 Milestone 4 (ADR-0071): the visible sky background shader --
# unconditional here, matching every other production shader pair's own
# identical placement/reasoning; declared immediately after the
# output-transform variants and before src/runtime, for the same
# add_dependencies()-ordering reason documented below.
add_subdirectory(shaders/sky)
# Plan 0027 Milestone 4 (ADR-0072 D-3): the shadow-casting shader --
# unconditional here, matching every other production shader pair's own
# identical placement/reasoning (the shadow-casting Pipeline is created
# unconditionally at Runtime startup, ADR-0072 D-1); declared
# immediately after sky and before src/runtime, for the same
# add_dependencies()-ordering reason documented below.
add_subdirectory(shaders/shadow_cast)
endif()
# Plan 0013 Section D1: must come after add_subdirectory(assets),
# add_subdirectory(shaders/minimal_renderer), (Plan 0018 Section P10)
# add_subdirectory(shaders/textured_quad), (Plan 0019 Section P11)
# add_subdirectory(shaders/lit_textured), (Plan 0023 Milestone 4)
# add_subdirectory(shaders/pbr_direct_lit), (Plan 0025 Milestone 5)
# add_subdirectory(shaders/pbr_ibl), (Plan 0024 Milestone 4)
# add_subdirectory(shaders/output_transform_unorm)/
# add_subdirectory(shaders/output_transform_srgb), (Plan 0026 Milestone 4)
# add_subdirectory(shaders/sky), and (Plan 0027 Milestone 4)
# add_subdirectory(shaders/shadow_cast) above -- atlantis_runtime
# add_dependencies() on minimal_mesh_shaders, textured_quad_shaders,
# lit_textured_shaders, and ${ATLANTIS_minimal_cube_TARGET}, all
# declared by those calls, and CMake's add_dependencies() requires the
# named target to already be declared at the point the call is parsed.
# Runtime is a product executable, not an example, so it is
# unconditional here, not inside the ATLANTIS_BUILD_EXAMPLES block
# below.
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
  add_subdirectory(tests/image_regression)
  add_subdirectory(tests/render_graph)
  add_subdirectory(tests/renderer)
  add_subdirectory(tests/shader_system)
  add_subdirectory(tests/tools/shader_compiler)
  add_subdirectory(tests/tools/asset_cooker)
  add_subdirectory(tests/runtime)
endif()
