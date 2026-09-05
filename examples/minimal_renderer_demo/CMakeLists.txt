add_executable(atlantis_minimal_renderer_demo main.cpp)

target_link_libraries(atlantis_minimal_renderer_demo
  PRIVATE
    Atlantis::Core
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::VulkanBackend
    Atlantis::RenderGraph
    Atlantis::Renderer
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    atlantis_compiler_warnings
)

# Plan 0008 Section 7/8: the build-tree, Shader-System-produced artifact
# set (four files: two .spv, two .refl.json), copied next to this
# executable's own build output -- replaces Plan 0007's checked-in
# shaders/minimal_renderer/*.spv copy. copy_if_different is a no-op when
# already up to date. add_dependencies() (not just the file-path
# reference below) makes CMake's own build-order guarantee actually
# apply -- this target is never scheduled to build until the shader
# pair's own stamp-backed target has finished.
# Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): both output-transform
# shader pairs added the same way -- this demo presents to a real
# swapchain (unlike the fixed-format fixtures/headless demo), so it
# needs both variants available.
# Plan 0027 Milestone 9 (ADR-0072 D-1/P9f): shadow_cast_shaders added the
# same way -- this demo's own drawFrame() call now needs a real, minimal
# shadow-casting Pipeline too (an empty shadowCasterDrawItems, no real
# occluder).
add_dependencies(atlantis_minimal_renderer_demo minimal_mesh_shaders output_transform_unorm_shaders
  output_transform_srgb_shaders shadow_cast_shaders)
add_custom_command(TARGET atlantis_minimal_renderer_demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.vert.spv"
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.vert.refl.json"
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.frag.spv"
      "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}/output_transform_srgb.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.refl.json"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders/"
)

# Plan 0007 Section 12: launches the demo with its own build output
# directory as the working directory, regardless of where the build/run
# command itself was invoked from -- required for the plain relative
# shader path this demo uses.
add_custom_target(run_minimal_renderer_demo
  COMMAND $<TARGET_FILE:atlantis_minimal_renderer_demo>
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>"
  DEPENDS atlantis_minimal_renderer_demo
)
