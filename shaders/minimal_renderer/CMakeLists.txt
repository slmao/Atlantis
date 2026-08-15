# Plan 0008 Section 7/8: this repository's own consumer of
# atlantis_add_slang_shader_pair() (defined in
# src/shader_system/CMakeLists.txt) -- requires
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call in the root CMakeLists.txt.
atlantis_add_slang_shader_pair(
  NAME minimal_mesh
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/minimal_mesh.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/minimal_renderer
  EXPECTED_CONTRACT minimal-renderer
)

# Mechanical deviation from Plan 0008 (no architectural effect): the
# PARENT_SCOPE set() inside atlantis_add_slang_shader_pair() itself
# (src/shader_system/CMakeLists.txt) only reaches THIS directory's own
# scope -- CMake's PARENT_SCOPE always targets the immediate calling
# scope (a function's own caller), never bubbles further up on its own.
# Consumers in a SIBLING directory (examples/minimal_renderer_demo,
# tests/vulkan_backend) are children of the root CMakeLists.txt, not of
# this directory, so they need this second, explicit propagation up to
# root's own scope -- confirmed empirically during this Plan's own
# implementation (the variable measured empty in a sibling directory
# without this line).
set(ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
