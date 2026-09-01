add_executable(atlantis_headless_rendering_demo main.cpp)

target_link_libraries(atlantis_headless_rendering_demo
  PRIVATE
    Atlantis::Core
    Atlantis::RHI
    Atlantis::VulkanBackend
    Atlantis::RenderGraph
    Atlantis::Renderer
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    atlantis_compiler_warnings
)

# Same build-tree Shader-System-produced artifact set
# examples/minimal_renderer_demo's own CMakeLists.txt copies -- this demo
# reuses the identical minimal_mesh shader pair, not a new one.
# Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): output_transform_unorm_shaders
# added the same way -- this demo's own colorFormat is fixed Rgba8Unorm.
add_dependencies(atlantis_headless_rendering_demo minimal_mesh_shaders output_transform_unorm_shaders)
add_custom_command(TARGET atlantis_headless_rendering_demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_headless_rendering_demo>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_headless_rendering_demo>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_headless_rendering_demo>/shaders/"
)

# Launches the demo with its own build output directory as the working
# directory, regardless of where the build/run command itself was invoked
# from -- required for the plain relative shader path this demo uses,
# same precedent as run_minimal_renderer_demo/run_frame_execution_demo.
add_custom_target(run_headless_rendering_demo
  COMMAND $<TARGET_FILE:atlantis_headless_rendering_demo>
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_headless_rendering_demo>"
  DEPENDS atlantis_headless_rendering_demo
)
