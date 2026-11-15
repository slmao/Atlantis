atlantis_add_slang_shader_pair(
  NAME pbr_ibl
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_ibl
  EXPECTED_CONTRACT pbr-ibl
)

set(ATLANTIS_pbr_ibl_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_ibl_SHADER_OUTPUT_DIR}" PARENT_SCOPE)

atlantis_add_slang_shader_pair(
  NAME pbr_ibl_occlusion
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_occlusion.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_ibl
  EXPECTED_CONTRACT pbr-ibl-occlusion
  SOURCE_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl.slang
)

atlantis_add_slang_shader_pair(
  NAME pbr_ibl_occlusion_packed
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl_occlusion_packed.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_ibl
  EXPECTED_CONTRACT pbr-ibl-occlusion-packed
  SOURCE_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/pbr_ibl.slang
)
