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
