# Plan 0019 Section P11/D8: atlantis_add_slang_shader_pair() (defined
# in src/shader_system/CMakeLists.txt) -- requires
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call, matching shaders/textured_quad/CMakeLists.txt's
# own established precedent exactly. Declared unconditionally in the
# root CMakeLists.txt (production, matching shaders/textured_quad's own
# post-Plan-0018 placement) -- atlantis_runtime needs it too, and the
# new lighting_demo fixture (test-only) consumes the same, already-built
# target.
atlantis_add_slang_shader_pair(
  NAME lit_textured
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/lit_textured.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/lit_textured
  EXPECTED_CONTRACT lit-textured
)

# Mechanical deviation from Plan 0008 (no architectural effect, matching
# shaders/textured_quad/CMakeLists.txt's own identical note): the
# PARENT_SCOPE set() inside atlantis_add_slang_shader_pair() itself only
# reaches this directory's own scope -- tests/image_regression/fixture/
# is a sibling of this directory (both children of the root
# CMakeLists.txt), so it needs this second, explicit propagation up to
# root's own scope.
set(ATLANTIS_lit_textured_SHADER_OUTPUT_DIR "${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
