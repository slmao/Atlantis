add_executable(atlantis_shader_system_tests
  json_parser_tests.cpp
  reflection_metadata_tests.cpp
  slang_json_transform_tests.cpp
  descriptor_contract_tests.cpp
  output_transform_reflection_cross_check_tests.cpp
  pbr_ibl_reflection_tests.cpp
  sky_reflection_tests.cpp
  command_line_tests.cpp
  version_provenance_tests.cpp
)

target_include_directories(atlantis_shader_system_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/shader_system/src
)

# Absolute path to tests/shader_system/fixtures/ -- independent of
# whatever working directory ctest invokes this executable from.
target_compile_definitions(atlantis_shader_system_tests
  PRIVATE
    ATLANTIS_SHADER_SYSTEM_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ATLANTIS_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
    ATLANTIS_OUTPUT_TRANSFORM_SRGB_SHADER_DIR="${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}"
    ATLANTIS_PBR_IBL_SHADER_DIR="${ATLANTIS_pbr_ibl_SHADER_OUTPUT_DIR}"
    ATLANTIS_SKY_SHADER_DIR="${ATLANTIS_sky_SHADER_OUTPUT_DIR}"
)

# Plan 0024 Milestone 8: cross-check both output-transform variants'
# real, always-fresh transformed reflection artifacts against their one
# shared C++ descriptor contract. The test is GPU-independent; these
# dependencies only run the existing shader build tools.
add_dependencies(atlantis_shader_system_tests
  output_transform_unorm_shaders
  output_transform_srgb_shaders
  pbr_ibl_shaders
  sky_shaders
)

target_link_libraries(atlantis_shader_system_tests
  PRIVATE
    Atlantis::ShaderSystem
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_shader_system_tests DISCOVERY_MODE PRE_TEST)

# Separate executable (Plan 0008 Section 9): links Atlantis::RHI, no
# Vulkan device needed -- pure data mapping. Kept separate from
# atlantis_shader_system_tests above so that target does not gain an
# RHI link it otherwise has no reason to need (mirroring
# atlantis_shader_system's own Core-only module boundary).
add_executable(atlantis_shader_system_rhi_integration_tests
  rhi_integration/vertex_input_mapping_tests.cpp
)

target_link_libraries(atlantis_shader_system_rhi_integration_tests
  PRIVATE
    Atlantis::ShaderSystemRhiIntegration
    Atlantis::RHI
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_shader_system_rhi_integration_tests DISCOVERY_MODE PRE_TEST)
