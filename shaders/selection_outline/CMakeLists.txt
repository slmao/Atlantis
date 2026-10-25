# Plan 0066 P3 (ADR-0132 D3): the selection outline's edge pass -- production content
# like editor_ui (whose CMake this mirrors, including the second PARENT_SCOPE
# propagation up to root). Not in Android's packaged shader list: an
# Android Runtime receives no overlay (Plan 0066 J11).
atlantis_add_slang_shader_pair(
  NAME selection_outline
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/selection_outline.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/selection_outline
  EXPECTED_CONTRACT selection-outline
)

set(ATLANTIS_selection_outline_SHADER_OUTPUT_DIR "${ATLANTIS_selection_outline_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
