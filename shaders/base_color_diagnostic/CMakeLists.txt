atlantis_add_slang_shader_pair(
  NAME base_color_diagnostic
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/base_color_diagnostic.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/base_color_diagnostic
  EXPECTED_CONTRACT textured-material
)
set(ATLANTIS_base_color_diagnostic_SHADER_OUTPUT_DIR
    "${ATLANTIS_base_color_diagnostic_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
