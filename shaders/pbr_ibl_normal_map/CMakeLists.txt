atlantis_add_slang_shader_pair(
  NAME pbr_ibl_normal_map
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_normal_map.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_ibl_normal_map
  EXPECTED_CONTRACT pbr-ibl-normal-map
)

set(ATLANTIS_pbr_ibl_normal_map_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_ibl_normal_map_SHADER_OUTPUT_DIR}" PARENT_SCOPE)

atlantis_add_slang_shader_pair(
  NAME pbr_ibl_normal_map_occlusion
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_normal_map_occlusion.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_ibl_normal_map
  EXPECTED_CONTRACT pbr-ibl-normal-map-occlusion
  SOURCE_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_normal_map.slang
)

atlantis_add_slang_shader_pair(
  NAME pbr_ibl_normal_map_occlusion_packed
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_normal_map_occlusion_packed.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_ibl_normal_map
  EXPECTED_CONTRACT pbr-ibl-normal-map-occlusion-packed
  SOURCE_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_normal_map.slang
)
