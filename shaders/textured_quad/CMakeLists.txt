# Plan 0016 Milestone 9 (D11): atlantis_add_slang_shader_pair() (defined
# in src/shader_system/CMakeLists.txt) -- requires
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call, matching shaders/minimal_renderer/CMakeLists.txt's
# own established precedent exactly. Test-only consumer
# (tests/image_regression/fixture/) -- added inside the
# ATLANTIS_BUILD_TESTS block, unlike minimal_renderer's own unconditional
# placement (atlantis_runtime also needs minimal_mesh_shaders).
atlantis_add_slang_shader_pair(
  NAME textured_quad
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/textured_quad.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/textured_quad
  EXPECTED_CONTRACT textured-material
)

# Mechanical deviation from Plan 0008 (no architectural effect, matching
# shaders/minimal_renderer/CMakeLists.txt's own identical note): the
# PARENT_SCOPE set() inside atlantis_add_slang_shader_pair() itself only
# reaches this directory's own scope -- tests/image_regression/fixture/
# is a sibling of this directory (both children of the root
# CMakeLists.txt), so it needs this second, explicit propagation up to
# root's own scope.
set(ATLANTIS_textured_quad_SHADER_OUTPUT_DIR "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
