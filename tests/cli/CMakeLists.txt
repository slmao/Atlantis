# Plan 0054 M4: Atlantis CLI -- the six commands against expected output, the
# get -> set round trip, the per-frame script runner (including
# scripts/north_star.txt on the default scene from the build catalog), and the
# CLI's and Connection's include/link boundary scans (Spec 0054 R8).
add_executable(atlantis_cli_tests
  cli_command_tests.cpp
  cli_script_tests.cpp
  cli_boundary_tests.cpp
  # Plan 0055 M6: the command tree, its JSON and the invocation driver.
  cli_tree_tests.cpp
)

target_link_libraries(atlantis_cli_tests
  PRIVATE
    Atlantis::Cli
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_cli_tests PRIVATE
  ATLANTIS_CLI_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/cli"
  ATLANTIS_CONNECTION_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/connection"
  ATLANTIS_CLI_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
  ATLANTIS_ASSET_CATALOG_PATH="${ATLANTIS_ASSET_CATALOG_PATH}"
  ATLANTIS_DEFAULT_SCENE_GUID="${ATLANTIS_integrated_showcase_demo_scene_GUID}"
)
add_dependencies(atlantis_cli_tests atlantis_asset_catalog)

catch_discover_tests(atlantis_cli_tests DISCOVERY_MODE PRE_TEST)
