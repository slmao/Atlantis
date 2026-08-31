# Plan 0024 Milestone 4: mirrors shaders/output_transform_unorm/CMakeLists.txt's
# own atlantis_add_slang_shader_pair() call exactly (itself mirroring
# shaders/pbr_direct_lit/CMakeLists.txt), requiring
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call (same precedent). Declared unconditionally in
# the root CMakeLists.txt, immediately after shaders/output_transform_unorm
# and before src/runtime -- this shader is production, built-in
# content, not test-only.
atlantis_add_slang_shader_pair(
  NAME output_transform_srgb
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/output_transform_srgb.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/output_transform_srgb
  EXPECTED_CONTRACT output-transform-srgb
)

# Mechanical deviation from Plan 0008 (no architectural effect, matching
# shaders/output_transform_unorm/CMakeLists.txt's own identical note):
# the PARENT_SCOPE set() inside atlantis_add_slang_shader_pair() itself
# only reaches this directory's own scope -- tests/image_regression/fixture/
# and src/runtime/ are siblings of this directory (all children of the
# root CMakeLists.txt), so they need this second, explicit propagation
# up to root's own scope.
set(ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR "${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
