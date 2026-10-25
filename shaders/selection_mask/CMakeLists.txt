# Plan 0066 P3 (ADR-0132 D3): the selection outline's mask pass -- production content
# like editor_ui (whose CMake this mirrors, including the second PARENT_SCOPE
# propagation up to root). Not in Android's packaged shader list: an
# Android Runtime receives no overlay (Plan 0066 J11).
atlantis_add_slang_shader_pair(
  NAME selection_mask
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/selection_mask.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/selection_mask
  EXPECTED_CONTRACT selection-mask
)

set(ATLANTIS_selection_mask_SHADER_OUTPUT_DIR "${ATLANTIS_selection_mask_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
