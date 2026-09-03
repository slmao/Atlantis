# Plan 0026 Milestone 4: mirrors shaders/output_transform_unorm/CMakeLists.txt's
# own atlantis_add_slang_shader_pair() call exactly, requiring
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call (same precedent). Declared unconditionally in
# the root CMakeLists.txt, after shaders/output_transform_srgb -- this
# shader is production, built-in content, not test-only.
atlantis_add_slang_shader_pair(
  NAME sky
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sky.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/sky
  EXPECTED_CONTRACT sky
)

# Mechanical deviation from Plan 0008 (no architectural effect, matching
# shaders/output_transform_unorm/CMakeLists.txt's own identical note):
# the PARENT_SCOPE set() inside atlantis_add_slang_shader_pair() itself
# only reaches this directory's own scope -- tests/image_regression/fixture/
# and src/runtime/ are siblings of this directory (all children of the
# root CMakeLists.txt), so they need this second, explicit propagation
# up to root's own scope.
set(ATLANTIS_sky_SHADER_OUTPUT_DIR "${ATLANTIS_sky_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
