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
  mesh_normal_round_trip_tests.cpp
  mesh_normal_validation_tests.cpp
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
  environment_metadata_tests.cpp
  environment_artifact_tests.cpp
  load_environment_tests.cpp
  environment_processing_tests.cpp
  cook_environment_tests.cpp
  environment_asset_cmake_declaration_tests.cpp
  texture_fixture_source_identity_tests.cpp
  material_source_tests.cpp
  material_artifact_tests.cpp
  material_metadata_tests.cpp
  cook_material_tests.cpp
  load_material_tests.cpp
  material_asset_cmake_declaration_tests.cpp
)

target_link_libraries(atlantis_asset_system_tests
  PRIVATE
    Atlantis::AssetSystem
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

target_include_directories(atlantis_asset_system_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/asset_system/src
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

target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_ibl_studio_ARTIFACT_PATH="${ATLANTIS_ibl_studio_ARTIFACT_PATH}"
  ATLANTIS_ibl_studio_METADATA_PATH="${ATLANTIS_ibl_studio_METADATA_PATH}"
)
add_dependencies(atlantis_asset_system_tests ${ATLANTIS_ibl_studio_TARGET})

# Plan 0018 Milestone 5: exercises atlantis_add_material_asset() end to
# end through the real build system -- --kind=material cooking and its
# own TEXTURE-not-previously-declared FATAL_ERROR guard -- without
# declaring the real Milestone 15 material asset yet. textured_quad_unorm
# is already declared above (assets/CMakeLists.txt, processed before
# tests/asset_system), so it is available here as the TEXTURE argument.
# Declared BEFORE the scene below so it can be named as that scene's own
# MATERIAL_DEPENDENCIES entry.
atlantis_add_material_asset(
  NAME cmake_declaration_test_material
  SOURCE _test_fixtures/cmake_material_declaration_test.material.txt
  TEXTURE textured_quad_unorm
)
add_dependencies(atlantis_asset_system_tests ${ATLANTIS_cmake_declaration_test_material_TARGET})
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_ARTIFACT_PATH="${ATLANTIS_cmake_declaration_test_material_ARTIFACT_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_METADATA_PATH="${ATLANTIS_cmake_declaration_test_material_METADATA_PATH}"
)

# Plan 0015 Step 5 (V13's own mechanism, V27's SceneCookError portion),
# widened by Plan 0018 Section P9: exercises atlantis_add_scene_asset()
# end to end through the real build system -- MESH_DEPENDENCIES,
# MATERIAL_DEPENDENCIES, TEXTURE_DEPENDENCIES, manifest generation, and
# --kind=scene cooking. minimal_cube/cmake_declaration_test_material/
# textured_quad_unorm are all already declared above, so they are
# available here as dependency entries. The scene source itself (Section
# D7's own "declared but unreferenced is fine" tolerance) never actually
# names a material= token -- this declaration proves manifest generation
# for the two new dependency kinds, independent of what the scene's own
# node list references.
atlantis_add_scene_asset(
  NAME cmake_declaration_test_scene
  SOURCE _test_fixtures/cmake_scene_declaration_test.scene.txt
  MESH_DEPENDENCIES minimal_cube
  MATERIAL_DEPENDENCIES cmake_declaration_test_material
  TEXTURE_DEPENDENCIES textured_quad_unorm
)
add_dependencies(atlantis_asset_system_tests ${ATLANTIS_cmake_declaration_test_scene_TARGET})
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH="${ATLANTIS_cmake_declaration_test_scene_ARTIFACT_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH="${ATLANTIS_cmake_declaration_test_scene_METADATA_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_MANIFEST_PATH="${ATLANTIS_cmake_declaration_test_scene_MANIFEST_PATH}"
  ATLANTIS_minimal_cube_LOGICAL_PATH="${ATLANTIS_minimal_cube_LOGICAL_PATH}"
  ATLANTIS_minimal_cube_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
  ATLANTIS_minimal_cube_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}"
  ATLANTIS_cmake_declaration_test_material_LOGICAL_PATH="${ATLANTIS_cmake_declaration_test_material_LOGICAL_PATH}"
  ATLANTIS_cmake_declaration_test_material_ARTIFACT_PATH="${ATLANTIS_cmake_declaration_test_material_ARTIFACT_PATH}"
  ATLANTIS_cmake_declaration_test_material_METADATA_PATH="${ATLANTIS_cmake_declaration_test_material_METADATA_PATH}"
  ATLANTIS_textured_quad_unorm_LOGICAL_PATH="${ATLANTIS_textured_quad_unorm_LOGICAL_PATH}"
)

catch_discover_tests(atlantis_asset_system_tests DISCOVERY_MODE PRE_TEST)
