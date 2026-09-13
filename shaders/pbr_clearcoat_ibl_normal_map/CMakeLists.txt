atlantis_add_slang_shader_pair(
  NAME pbr_clearcoat_ibl_normal_map
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_clearcoat_ibl_normal_map.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_clearcoat_ibl_normal_map
  EXPECTED_CONTRACT pbr-clearcoat-ibl-normal-map
)

set(ATLANTIS_pbr_clearcoat_ibl_normal_map_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_clearcoat_ibl_normal_map_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
