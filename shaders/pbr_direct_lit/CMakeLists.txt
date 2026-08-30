# Plan 0023 Milestone 4: mirrors shaders/lit_textured/CMakeLists.txt's
# own atlantis_add_slang_shader_pair() call exactly, requiring
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call (same precedent). Declared unconditionally in
# the root CMakeLists.txt, immediately after shaders/lit_textured and
# before src/runtime -- this shader is production, built-in content,
# not test-only, matching lit_textured's own placement/reasoning
# exactly.
atlantis_add_slang_shader_pair(
  NAME pbr_direct_lit
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_direct_lit.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_direct_lit
  EXPECTED_CONTRACT pbr-direct-lit
)

# Mechanical deviation from Plan 0008 (no architectural effect, matching
# shaders/lit_textured/CMakeLists.txt's own identical note): the
# PARENT_SCOPE set() inside atlantis_add_slang_shader_pair() itself only
# reaches this directory's own scope -- tests/image_regression/fixture/
# is a sibling of this directory (both children of the root
# CMakeLists.txt), so it needs this second, explicit propagation up to
# root's own scope.
set(ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
