add_executable(atlantis_asset_system_tests
  rename_overwrite_probe.cpp
  static_mesh_asset_data_tests.cpp
  logical_path_tests.cpp
  asset_id_tests.cpp
  asset_set_validation_tests.cpp
  mesh_source_tests.cpp
  asset_metadata_tests.cpp
  mesh_artifact_tests.cpp
  mesh_uv_round_trip_tests.cpp
  load_tests.cpp
  textured_quad_mesh_tests.cpp
  module_boundary_tests.cpp
  validated_scene_data_tests.cpp
  scene_source_tests.cpp
  cook_scene_tests.cpp
  decode_scene_tests.cpp
  scene_asset_cmake_declaration_tests.cpp
  texture_metadata_tests.cpp
  texture_artifact_tests.cpp
  cook_texture_tests.cpp
  load_texture_tests.cpp
  texture_fixture_source_identity_tests.cpp
  material_source_tests.cpp
  material_artifact_tests.cpp
  material_metadata_tests.cpp
  cook_material_tests.cpp
  load_material_tests.cpp
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

# Plan 0016's own "Human Review Correction -- 2026-08-24" (Verification
# V45/V46/V49): the textured fixture's own two source PNGs and cooked
# artifact/metadata paths -- textured_quad_unorm/textured_quad_srgb are
# already declared unconditionally in assets/CMakeLists.txt (like
# minimal_cube/world_scene above), so their ARTIFACT/METADATA path
# variables are already available here, matching the
# ATLANTIS_minimal_cube_ARTIFACT_PATH precedent above and
# tests/image_regression/CMakeLists.txt's own identical use of the same
# two targets.
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_TEXTURED_QUAD_SOURCE_UNORM_PATH="${CMAKE_SOURCE_DIR}/assets/textures/textured_quad_source_unorm.png"
  ATLANTIS_TEXTURED_QUAD_SOURCE_SRGB_PATH="${CMAKE_SOURCE_DIR}/assets/textures/textured_quad_source_srgb.png"
  ATLANTIS_textured_quad_unorm_ARTIFACT_PATH="${ATLANTIS_textured_quad_unorm_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_unorm_METADATA_PATH="${ATLANTIS_textured_quad_unorm_METADATA_PATH}"
  ATLANTIS_textured_quad_srgb_ARTIFACT_PATH="${ATLANTIS_textured_quad_srgb_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_srgb_METADATA_PATH="${ATLANTIS_textured_quad_srgb_METADATA_PATH}"
)
add_dependencies(atlantis_asset_system_tests
  ${ATLANTIS_textured_quad_unorm_TARGET} ${ATLANTIS_textured_quad_srgb_TARGET})

# Plan 0017 Milestone 2: the two independent quad mesh assets declared
# in assets/CMakeLists.txt, made visible to
# textured_quad_mesh_tests.cpp exactly like the two texture assets
# above.
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_textured_quad_left_ARTIFACT_PATH="${ATLANTIS_textured_quad_left_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_left_METADATA_PATH="${ATLANTIS_textured_quad_left_METADATA_PATH}"
  ATLANTIS_textured_quad_right_ARTIFACT_PATH="${ATLANTIS_textured_quad_right_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_right_METADATA_PATH="${ATLANTIS_textured_quad_right_METADATA_PATH}"
)
add_dependencies(atlantis_asset_system_tests
  ${ATLANTIS_textured_quad_left_TARGET} ${ATLANTIS_textured_quad_right_TARGET})

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
