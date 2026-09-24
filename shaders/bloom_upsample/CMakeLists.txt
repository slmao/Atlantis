# Plan 0044 Milestone 1 (Spec 0044, ADR-0092): a bloom shader pair,
# production content like the output-transform pairs (whose CMake this
# mirrors, including the second PARENT_SCOPE propagation up to root).
atlantis_add_slang_shader_pair(
  NAME bloom_upsample
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bloom_upsample.slang
  VERTEX_ENTRY vertexMain
  FRAGMENT_ENTRY fragmentMain
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/bloom_upsample
  EXPECTED_CONTRACT bloom-upsample
)

set(ATLANTIS_bloom_upsample_SHADER_OUTPUT_DIR "${ATLANTIS_bloom_upsample_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
