# GPU-independent: exercises runCookCommand()'s argument-handling logic
# directly (no real subprocess), matching
# tests/tools/shader_compiler/CMakeLists.txt's own
# atlantis_shader_compiler_process_launch_tests precedent.
add_executable(atlantis_asset_cooker_command_tests
  cook_command_tests.cpp
  texture_cook_command_tests.cpp
  environment_cook_command_tests.cpp
)

target_link_libraries(atlantis_asset_cooker_command_tests
  PRIVATE
    atlantis_asset_cooker_lib
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# Plan 0016 Section D9 (V-item 7): an absolute, configure-time path to
# this test's own checked-in tiny real PNG fixtures (RGBA/RGB/grayscale/
# corrupted) -- matching ATLANTIS_ASSET_SYSTEM_SOURCE_DIR's own
# absolute-path-definition precedent, independent of whatever working
# directory ctest invokes this executable from.
target_compile_definitions(atlantis_asset_cooker_command_tests PRIVATE
  ATLANTIS_ASSET_COOKER_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

catch_discover_tests(atlantis_asset_cooker_command_tests DISCOVERY_MODE PRE_TEST)

# "tool"-labeled (Plan 0012 Section D7): launches the real
# atlantis_asset_cooker executable as a subprocess to prove the
# byte-identical determinism claim end to end -- needs no GPU/Vulkan
# device, matching tests/tools/shader_compiler/CMakeLists.txt's own
# "tool" label precedent (a distinct CTest label from "gpu").
add_executable(atlantis_asset_cooker_determinism_tests
  cooker_determinism_tests.cpp
)

target_compile_definitions(atlantis_asset_cooker_determinism_tests PRIVATE
  ATLANTIS_ASSET_COOKER_EXECUTABLE="$<TARGET_FILE:atlantis_asset_cooker>"
  ATLANTIS_IBL_STUDIO_SOURCE_PATH="${CMAKE_SOURCE_DIR}/assets/environments/ibl_studio_source.hdr"
  ATLANTIS_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
)

target_link_libraries(atlantis_asset_cooker_determinism_tests
  PRIVATE
    atlantis_asset_cooker_lib
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

add_dependencies(atlantis_asset_cooker_determinism_tests atlantis_asset_cooker)

catch_discover_tests(atlantis_asset_cooker_determinism_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "tool"
)
