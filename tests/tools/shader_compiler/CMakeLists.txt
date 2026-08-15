# GPU-independent (Plan 0008 Section 9): exercises the argv/quoting pure
# functions in isolation, plus launchProcess()'s own happy/error paths
# via cmd.exe -- a standard Windows component, not an SDK-provided tool,
# so this needs no Vulkan SDK and stays out of the "tool" label.
add_executable(atlantis_shader_compiler_process_launch_tests
  process_launch_tests.cpp
)

target_link_libraries(atlantis_shader_compiler_process_launch_tests
  PRIVATE
    atlantis_shader_compiler_lib
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_shader_compiler_process_launch_tests DISCOVERY_MODE PRE_TEST)

# "tool"-labeled (Plan 0008 Section 9): needs the real Vulkan-SDK-
# provided slangc/spirv-val at test-run time, but no GPU/Vulkan device
# -- a distinct CTest label from the existing "gpu" one.
add_executable(atlantis_shader_compiler_toolchain_integration_tests
  toolchain_integration_tests.cpp
)

target_compile_definitions(atlantis_shader_compiler_toolchain_integration_tests
  PRIVATE
    ATLANTIS_SHADER_COMPILER_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(atlantis_shader_compiler_toolchain_integration_tests
  PRIVATE
    atlantis_shader_compiler_lib
    Atlantis::ShaderSystem
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_shader_compiler_toolchain_integration_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "tool"
)
