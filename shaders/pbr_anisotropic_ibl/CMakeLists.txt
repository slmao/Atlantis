atlantis_add_slang_shader_pair(
  NAME pbr_anisotropic_ibl
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/pbr_anisotropic_ibl.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr_anisotropic_ibl
  EXPECTED_CONTRACT pbr-anisotropic-ibl
)

set(ATLANTIS_pbr_anisotropic_ibl_SHADER_OUTPUT_DIR "${ATLANTIS_pbr_anisotropic_ibl_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
