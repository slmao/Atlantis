# Plan 0027 Milestone 4: mirrors shaders/sky/CMakeLists.txt's own
# atlantis_add_slang_shader_pair() call exactly, requiring
# add_subdirectory(src/shader_system) to precede this directory's own
# add_subdirectory() call (same precedent). Declared unconditionally in
# the root CMakeLists.txt -- this shader is production, built-in
# content, not test-only (the shadow-casting Pipeline is created
# unconditionally at Runtime startup, ADR-0072 D-1).
atlantis_add_slang_shader_pair(
  NAME shadow_cast
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shadow_cast.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/shadow_cast
  EXPECTED_CONTRACT shadow-cast
)

# Mechanical deviation from Plan 0008 (no architectural effect, matching
# shaders/sky/CMakeLists.txt's own identical note): the PARENT_SCOPE
# set() inside atlantis_add_slang_shader_pair() itself only reaches this
# directory's own scope -- every consuming CMakeLists.txt is a sibling
# of this directory (all children of the root CMakeLists.txt), so they
# need this second, explicit propagation up to root's own scope.
set(ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
