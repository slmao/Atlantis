add_executable(atlantis_asset_system_tests
  rename_overwrite_probe.cpp
  static_mesh_asset_data_tests.cpp
  logical_path_tests.cpp
  asset_id_tests.cpp
  asset_guid_tests.cpp
  entity_ref_tests.cpp
  asset_catalog_source_tests.cpp
  asset_catalog_tests.cpp
  asset_declarations_tests.cpp
  sidecar_asset_guid_tests.cpp
  guid_reference_tests.cpp
  mesh_source_tests.cpp
  mesh_artifact_u32_tests.cpp
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
  mesh_tangent_generation_tests.cpp
  asset_system_schema_tests.cpp
  scene_semantic_types_tests.cpp
  authoring_scene_mapping_tests.cpp
  scene_semantic_schema_tests.cpp
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
  # Plan 0047 M2 (P10): the generated declarations list and the Bistro
  # content gate.
  ATLANTIS_ASSET_DECLARATIONS_PATH="${CMAKE_BINARY_DIR}/assets/declarations.txt"
  ATLANTIS_BISTRO_CONTENT_DIR="${ATLANTIS_BISTRO_CONTENT_DIR}"
  # Plan 0047 M3: the committed catalog source the cooked fixtures' GUIDs come from.
  ATLANTIS_ASSET_CATALOG_SOURCE_PATH="${CMAKE_SOURCE_DIR}/assets/asset_catalog.txt"
  # Plan 0049 P10: the committed scene-source corpus, enumerated at run time.
  ATLANTIS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

# Plan 0029 Section P1: an absolute, configure-time path to the real
# authoring-source mesh directory -- mesh_tangent_generation_tests.cpp
# reads all 5 real, committed .mesh.txt files directly (parseMeshSource()
# + generateTangents(), no cooking/artifact round trip needed) to
# re-confirm the Existing-mesh tangent-generatability audit table
# against real, current source, not a hand-copied fixture.
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_ASSETS_MESHES_DIR="${CMAKE_SOURCE_DIR}/assets/meshes"
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
# end through the real build system -- --kind=material cooking.
atlantis_add_material_asset(
  NAME cmake_declaration_test_material
  SOURCE _test_fixtures/cmake_material_declaration_test.material.txt
)
add_dependencies(atlantis_asset_system_tests ${ATLANTIS_cmake_declaration_test_material_TARGET})
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_ARTIFACT_PATH="${ATLANTIS_cmake_declaration_test_material_ARTIFACT_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_METADATA_PATH="${ATLANTIS_cmake_declaration_test_material_METADATA_PATH}"
)

# Plan 0015 Step 5 (V13's own mechanism, V27's SceneCookError portion),
# Plan 0047 M5: exercises atlantis_add_scene_asset() end to end through the
# real build system -- --kind=scene cooking, the configure-time GUID export and
# the assembled catalog's record for the scene.
atlantis_add_scene_asset(
  NAME cmake_declaration_test_scene
  SOURCE _test_fixtures/cmake_scene_declaration_test.scene.txt
)
add_dependencies(atlantis_asset_system_tests ${ATLANTIS_cmake_declaration_test_scene_TARGET}
  atlantis_asset_catalog)
target_compile_definitions(atlantis_asset_system_tests PRIVATE
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH="${ATLANTIS_cmake_declaration_test_scene_ARTIFACT_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH="${ATLANTIS_cmake_declaration_test_scene_METADATA_PATH}"
  ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_GUID="${ATLANTIS_cmake_declaration_test_scene_GUID}"
  ATLANTIS_ASSET_CATALOG_PATH="${ATLANTIS_ASSET_CATALOG_PATH}"
)

# Plan 0048 M3: asset_system_schema_tests.cpp's no-default enumerator canary
# switches need MSVC's C4062, off by default even at /W4 -- the same
# target-scoped option tests/world/CMakeLists.txt already uses.
target_compile_options(atlantis_asset_system_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)

catch_discover_tests(atlantis_asset_system_tests DISCOVERY_MODE PRE_TEST)
