atlantis_add_slang_shader_pair(
  NAME pbr_direct_lit_normal_map
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_direct_lit_normal_map.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_direct_lit_normal_map
  EXPECTED_CONTRACT pbr-direct-lit-normal-map
)

set(ATLANTIS_pbr_direct_lit_normal_map_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_direct_lit_normal_map_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
