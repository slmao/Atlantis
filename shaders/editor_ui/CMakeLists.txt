# Plan 0056 P5 (ADR-0108 D3): the UI overlay pass's shader pair, production
# content like the output-transform pairs (whose CMake this mirrors, including
# the second PARENT_SCOPE propagation up to root).
atlantis_add_slang_shader_pair(
  NAME editor_ui
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/editor_ui.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/editor_ui
  EXPECTED_CONTRACT editor-ui
)

set(ATLANTIS_editor_ui_SHADER_OUTPUT_DIR "${ATLANTIS_editor_ui_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
