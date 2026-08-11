add_executable(atlantis_minimal_renderer_demo main.cpp)

target_link_libraries(atlantis_minimal_renderer_demo
  PRIVATE
    Atlantis::Core
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::VulkanBackend
    Atlantis::RenderGraph
    Atlantis::Renderer
    atlantis_compiler_warnings
)

# Plan 0007 Section 12/17: shaders/minimal_renderer's checked-in SPIR-V
# pair, copied next to this executable's own build output.
# copy_if_different is a no-op when already up to date.
add_custom_command(TARGET atlantis_minimal_renderer_demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/shaders/minimal_renderer/minimal_mesh.vert.spv"
      "${CMAKE_SOURCE_DIR}/shaders/minimal_renderer/minimal_mesh.frag.spv"
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
