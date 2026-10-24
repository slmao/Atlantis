# Plan 0066 P3 (ADR-0132 D3): the Viewport overlay's primitives -- segments,
# triangles and points -- production content
# like editor_ui (whose CMake this mirrors, including the second PARENT_SCOPE
# propagation up to root). Not in Android's packaged shader list: an
# Android Runtime receives no overlay (Plan 0066 J11).
atlantis_add_slang_shader_pair(
  NAME viewport_overlay
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/viewport_overlay.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/viewport_overlay
  EXPECTED_CONTRACT viewport-overlay
)

set(ATLANTIS_viewport_overlay_SHADER_OUTPUT_DIR "${ATLANTIS_viewport_overlay_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
