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
  cook_scene_tests.cpp
  decode_scene_tests.cpp
  scene_asset_cmake_declaration_tests.cpp
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

# Plan 0015 Step 5 (V13's own mechanism, V27's SceneCookError portion):
# exercises atlantis_add_scene_asset() end to end through the real
# build system -- MESH_DEPENDENCIES, manifest generation, and
# --kind=scene cooking -- without declaring the real Step 9 scene
# asset yet (Section D7's own Milestone 5 scope). minimal_cube is
# already declared by add_subdirectory(assets) (root CMakeLists.txt,
# processed before tests/asset_system), so it is available here as a
# MESH_DEPENDENCIES entry.
atlantis_add_scene_asset(
  NAME cmake_declaration_test_scene
  SOURCE _test_fixtures/cmake_scene_declaration_test.scene.txt
  MESH_DEPENDENCIES minimal_cube
)
add_dependencies(atlantis_asset_system_tests ${ATLANTIS_cmake_declaration_test_scene_TARGET})
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH="${ATLANTIS_cmake_declaration_test_scene_ARTIFACT_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH="${ATLANTIS_cmake_declaration_test_scene_METADATA_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_MANIFEST_PATH="${ATLANTIS_cmake_declaration_test_scene_MANIFEST_PATH}"
  ATLANTIS_minimal_cube_LOGICAL_PATH="${ATLANTIS_minimal_cube_LOGICAL_PATH}"
  ATLANTIS_minimal_cube_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
  ATLANTIS_minimal_cube_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}"
)

catch_discover_tests(atlantis_asset_system_tests DISCOVERY_MODE PRE_TEST)

