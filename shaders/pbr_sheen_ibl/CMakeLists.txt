atlantis_add_slang_shader_pair(
  NAME pbr_sheen_ibl
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_sheen_ibl.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_sheen_ibl
  EXPECTED_CONTRACT pbr-sheen-ibl
)

set(ATLANTIS_pbr_sheen_ibl_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_sheen_ibl_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
