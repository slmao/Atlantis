add_executable(atlantis_shader_system_tests
  json_parser_tests.cpp
  reflection_metadata_tests.cpp
  slang_json_transform_tests.cpp
  descriptor_contract_tests.cpp
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
)

target_link_libraries(atlantis_shader_system_tests
  PRIVATE
    Atlantis::ShaderSystem
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_shader_system_tests DISCOVERY_MODE PRE_TEST)
