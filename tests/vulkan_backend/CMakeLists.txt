# See plans/0003-rhi-vulkan-windowed-foundation.md Section 8: this
# executable is GPU-independent by construction (no source file here
# touches a real VkInstance/VkDevice) and is discovered by a bare
# ctest -LE gpu run with no label to exclude. The separate, GPU-required
# atlantis_vulkan_backend_gpu_tests executable below requires a real
# Vulkan-capable GPU and a real window; every case it registers carries
# the CTest "gpu" label, so it is excluded from `ctest -LE gpu` and run
# only via `ctest -L gpu`.

add_executable(atlantis_vulkan_backend_tests
  vulkan_result_tests.cpp
  presentation_logic_tests.cpp
  validation_policy_tests.cpp
  resource_state_mapping_tests.cpp
  dynamic_rendering_tests.cpp
  instance_api_version_tests.cpp
  device_extension_list_tests.cpp
  dynamic_rendering_entry_points_tests.cpp
  vulkan_memory_tests.cpp
  descriptor_pool_growth_tests.cpp
  hdr_color_target_capability_tests.cpp
)

target_include_directories(atlantis_vulkan_backend_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src
)

target_link_libraries(atlantis_vulkan_backend_tests
  PRIVATE
    Atlantis::VulkanBackend
    # atlantis_vulkan_backend links Vulkan::Vulkan PRIVATE (Section 10), so
    # it does not propagate here -- this target needs its own link because
    # it directly compiles private headers (vulkan_result.h,
    # vulkan_presentation.h) that include Vulkan headers.
    Vulkan::Vulkan
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST: see tests/core/CMakeLists.txt's comment for why POST_BUILD
# (the default) is unsuitable for a multi-config generator.
catch_discover_tests(atlantis_vulkan_backend_tests DISCOVERY_MODE PRE_TEST)

# Separate executable, separate registration -- not a second
# catch_discover_tests() call or a TEST_SPEC filter against the target
# above, so there is no risk of the same Catch2 test case being
# registered twice. Every pre-existing file here drives Presentation
# entirely through Atlantis's public API (atlantis/platform/platform.h,
# atlantis/vulkan_backend/vulkan_backend.h, atlantis/rhi/presentation.h)
# and includes no Vulkan header itself, so it needs no Vulkan::Vulkan
# link. Plan 0021 Milestone 3: descriptor_pool_growth_gpu_tests.cpp is
# the one exception -- it #includes vulkan_descriptor_pool_growth.h
# directly (via the new PRIVATE include path below) so its own real
# capacity-total assertions are computed from the production constants
# themselves, never a hand-copied literal that could silently drift.
# That header has no Vulkan dependency of its own (<array>/<cstddef>/
# <cstdint> only), so this remains a pure include-path addition -- no
# new Vulkan::Vulkan link is needed for it either.
add_executable(atlantis_vulkan_backend_gpu_tests
  vulkan_presentation_gpu_tests.cpp
  frame_execution_gpu_tests.cpp
  minimal_renderer_gpu_tests.cpp
  headless_rendering_gpu_tests.cpp
  texture_upload_gpu_tests.cpp
  descriptor_pool_growth_gpu_tests.cpp
  pipeline_depth_write_gpu_tests.cpp
)

target_include_directories(atlantis_vulkan_backend_gpu_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src
)

target_link_libraries(atlantis_vulkan_backend_gpu_tests
  PRIVATE
    Atlantis::VulkanBackend
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::RenderGraph
    Atlantis::Renderer
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# LABELS "gpu": the CMake test-property mechanism that actually makes
# `ctest -L gpu` / `ctest -LE gpu` work -- not a Catch2-level tag filter.
# PRE_TEST: same multi-config rationale as atlantis_vulkan_backend_tests
# above. WORKING_DIRECTORY (Plan 0007 Section 12): minimal_renderer_gpu_tests.cpp
# opens the shader .spv pair by a plain relative path ("shaders/..."),
# resolved against this target's own build output directory -- harmless
# for every pre-existing case in this file, none of which opens any file
# by a relative path.
catch_discover_tests(atlantis_vulkan_backend_gpu_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "gpu"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>"
)

# Plan 0007 Section 12/17: shaders/minimal_renderer's checked-in SPIR-V
# pair, copied next to this executable's own build output.
# copy_if_different is a no-op when already up to date.
# Plan 0008 Section 7/8: the build-tree, Shader-System-produced artifact
# set, copied next to this executable's own build output -- replaces
# Plan 0007's checked-in shaders/minimal_renderer/*.spv copy.
# Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): both output-transform
# shader pairs -- minimal_renderer_gpu_tests.cpp presents to a real
# swapchain (needs both variants); headless_rendering_gpu_tests.cpp/
# descriptor_pool_growth_gpu_tests.cpp use a fixed Rgba8Unorm
# OffscreenTarget (unorm only), but both are copied here unconditionally
# since all three share this one executable/copy step.
add_dependencies(atlantis_vulkan_backend_gpu_tests minimal_mesh_shaders output_transform_unorm_shaders
  output_transform_srgb_shaders)
add_custom_command(TARGET atlantis_vulkan_backend_gpu_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.vert.spv"
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.vert.refl.json"
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.frag.spv"
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>/shaders/"
)
