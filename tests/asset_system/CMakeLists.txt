add_executable(atlantis_asset_system_tests
  rename_overwrite_probe.cpp
  static_mesh_asset_data_tests.cpp
  logical_path_tests.cpp
  asset_id_tests.cpp
  asset_set_validation_tests.cpp
  mesh_source_tests.cpp
  asset_metadata_tests.cpp
  mesh_artifact_tests.cpp
  load_tests.cpp
  module_boundary_tests.cpp
  validated_scene_data_tests.cpp
  scene_source_tests.cpp
)

target_link_libraries(atlantis_asset_system_tests
  PRIVATE
    Atlantis::AssetSystem
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# Plan 0012 V9: an absolute, configure-time path to scan for forbidden
# includes -- does not depend on WORKING_DIRECTORY, matching the
# ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR precedent in
# tests/image_regression/CMakeLists.txt.
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_ASSET_SYSTEM_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/asset_system"
)

catch_discover_tests(atlantis_asset_system_tests DISCOVERY_MODE PRE_TEST)
