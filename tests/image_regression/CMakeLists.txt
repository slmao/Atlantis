add_subdirectory(support)
add_subdirectory(fixture)
add_subdirectory(golden_generator)

# GPU-independent test executable (Section 5.1/5.3): the pixel-diff
# algorithm, PNG codec round-trip, provenance parsing, and the golden
# validity check's four steps, all exercised against synthetic in-memory
# buffers and constructed test PNGs -- no Vulkan device needed. Sibling
# of the library it tests, matching every other tests/<module>/CMakeLists.txt
# precedent.
add_executable(atlantis_image_regression_tests
  pixel_diff_tests.cpp
  png_codec_tests.cpp
  provenance_tests.cpp
  golden_validity_tests.cpp
  tone_mapping_reference_tests.cpp
)

target_link_libraries(atlantis_image_regression_tests
  PRIVATE
    Atlantis::ImageRegressionSupport
    # PRIVATE: png_codec_tests.cpp/golden_validity_tests.cpp call
    # stbi_write_png() directly to construct deliberately-non-RGBA8 test
    # fixtures at run time (Section 5.1) -- declarations only, resolved
    # at link time against the one implementation already compiled into
    # atlantis_image_regression_support (png_codec.cpp).
    Stb::Stb
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST: see tests/core/CMakeLists.txt's comment for why POST_BUILD
# (the default) is unsuitable for a multi-config generator. No LABELS
# "gpu" anywhere in this block -- this executable is entirely
# GPU-independent, matching tests/render_graph/CMakeLists.txt's own
# precedent.
catch_discover_tests(atlantis_image_regression_tests DISCOVERY_MODE PRE_TEST)

# GPU-required test executable (Section 5.2/5.4): the real
# capture-via-OffscreenTarget -> compare-against-checked-in-golden path.
add_executable(atlantis_image_regression_gpu_tests
  image_regression_gpu_tests.cpp
  world_scene_gpu_tests.cpp
  world_scene_loaded_gpu_tests.cpp
  textured_quad_gpu_tests.cpp
  material_demo_gpu_tests.cpp
  lighting_demo_gpu_tests.cpp
  pbr_material_demo_gpu_tests.cpp
  hdr_roll_off_demo_gpu_tests.cpp
)

target_link_libraries(atlantis_image_regression_gpu_tests
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    # Plan 0014 Section D10: world_scene_gpu_tests.cpp names World-scene
    # fixture types directly.
    Atlantis::World
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# LABELS "gpu": the existing plain ctest convention Spec 0006/Spec 0010
# already established -- no new CI/test-category label introduced.
catch_discover_tests(atlantis_image_regression_gpu_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "gpu"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>"
)

# Absolute, configure-time paths for everything this executable's own
# test code needs to find that is NOT the shader pair -- goldens, the
# machine-local environment file, where to write failure diagnostics,
# and (Plan 0012 Step 6) where cooked Asset System artifacts land. None
# of the four depends on WORKING_DIRECTORY.
target_compile_definitions(atlantis_image_regression_gpu_tests PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR="${CMAKE_BINARY_DIR}/image_regression_failures"
  ATLANTIS_ASSET_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/assets"
  # Plan 0015 Section D11/Step 9: world_scene_loaded_gpu_tests.cpp's own
  # real, cooked scene asset paths.
  ATLANTIS_WORLD_SCENE_ARTIFACT_PATH="${ATLANTIS_world_scene_ARTIFACT_PATH}"
  ATLANTIS_WORLD_SCENE_METADATA_PATH="${ATLANTIS_world_scene_METADATA_PATH}"
  ATLANTIS_WORLD_SCENE_MANIFEST_PATH="${ATLANTIS_world_scene_MANIFEST_PATH}"
  # Plan 0016 Milestone 9: the textured fixture's own two cooked texture
  # artifact/metadata paths (Milestone 6/7).
  ATLANTIS_textured_quad_unorm_ARTIFACT_PATH="${ATLANTIS_textured_quad_unorm_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_unorm_METADATA_PATH="${ATLANTIS_textured_quad_unorm_METADATA_PATH}"
  ATLANTIS_textured_quad_srgb_ARTIFACT_PATH="${ATLANTIS_textured_quad_srgb_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_srgb_METADATA_PATH="${ATLANTIS_textured_quad_srgb_METADATA_PATH}"
  # Plan 0017 Milestone 3: the textured fixture's own two cooked static
  # mesh artifact/metadata paths (Milestone 2).
  ATLANTIS_textured_quad_left_ARTIFACT_PATH="${ATLANTIS_textured_quad_left_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_left_METADATA_PATH="${ATLANTIS_textured_quad_left_METADATA_PATH}"
  ATLANTIS_textured_quad_right_ARTIFACT_PATH="${ATLANTIS_textured_quad_right_ARTIFACT_PATH}"
  ATLANTIS_textured_quad_right_METADATA_PATH="${ATLANTIS_textured_quad_right_METADATA_PATH}"
  # Plan 0018 Milestone 16: the material_demo fixture's own real, cooked
  # scene artifact/metadata/manifest paths and the MaterialKind::UnlitTextured
  # built-in shader pair's own output directory.
  ATLANTIS_material_demo_scene_ARTIFACT_PATH="${ATLANTIS_material_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_material_demo_scene_METADATA_PATH="${ATLANTIS_material_demo_scene_METADATA_PATH}"
  ATLANTIS_material_demo_scene_MANIFEST_PATH="${ATLANTIS_material_demo_scene_MANIFEST_PATH}"
  ATLANTIS_MATERIAL_DEMO_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  # Plan 0019 Section P6: MaterialDemoFixture's own widened
  # realizePendingMaterials() call requires a real litTextured* trio too
  # (material_demo_gpu_tests.cpp's own buildTestConfig()), even though
  # material_demo_scene never actually realizes one.
  ATLANTIS_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  # Plan 0023 Milestone 5: MaterialDemoFixture's own further-widened
  # realizePendingMaterials() call requires a real pbrDirectLit* trio
  # too, even though material_demo_scene never actually realizes one.
  ATLANTIS_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  # Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): MaterialDemoFixture's
  # own output-transform Pipeline -- this fixture's colorFormat is fixed
  # Rgba8Unorm, so only the UNORM variant's own output directory is
  # ever needed.
  ATLANTIS_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  # Plan 0019 Section P10/Milestone 10: the lighting_demo fixture's own
  # real, cooked scene artifact/metadata/manifest paths and both built-in
  # shader pairs' own output directories (LightingDemoFixture needs both,
  # mirroring MaterialDemoFixture's own identical need above).
  ATLANTIS_lighting_demo_scene_ARTIFACT_PATH="${ATLANTIS_lighting_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_lighting_demo_scene_METADATA_PATH="${ATLANTIS_lighting_demo_scene_METADATA_PATH}"
  ATLANTIS_lighting_demo_scene_MANIFEST_PATH="${ATLANTIS_lighting_demo_scene_MANIFEST_PATH}"
  ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  # Plan 0023 Milestone 5: LightingDemoFixture's own further-widened
  # realizePendingMaterials() call requires a real pbrDirectLit* trio
  # too, even though lighting_demo_scene never actually realizes one.
  ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  # Plan 0024 Milestone 7: LightingDemoFixture's own output-transform
  # Pipeline -- mirrors ATLANTIS_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR's
  # own identical need above.
  ATLANTIS_LIGHTING_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  # Plan 0023 Milestone 8: the pbr_material_demo fixture's own real,
  # cooked scene artifact/metadata/manifest paths and all three built-in
  # shader pairs' own output directories, mirroring
  # ATLANTIS_LIGHTING_DEMO_*'s own identical need above.
  ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH="${ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_pbr_material_demo_scene_METADATA_PATH="${ATLANTIS_pbr_material_demo_scene_METADATA_PATH}"
  ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH="${ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH}"
  ATLANTIS_PBR_MATERIAL_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_PBR_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  ATLANTIS_PBR_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  # Plan 0024 Milestone 7: PbrMaterialDemoFixture's own output-transform
  # Pipeline -- mirrors ATLANTIS_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR's
  # own identical need above.
  ATLANTIS_PBR_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  # Plan 0024 Milestone 10: the HDR roll-off fixture deliberately
  # reuses the PBR fixture path with its own focused scene asset.
  ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH="${ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH="${ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH}"
  ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH="${ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
)

# Plan 0012 Step 6: the asset-sourced GPU test case needs
# minimal_cube's own cooked artifact/metadata to already exist. Plan
# 0015 Section D11/Step 9: world_scene's own cooked artifact/metadata/
# manifest likewise. Plan 0017 Milestone 3: textured_quad_left/
# textured_quad_right likewise. Plan 0018 Milestone 16: material_demo_scene
# likewise. Plan 0019 Section P10/Milestone 10: lighting_demo_scene
# likewise. Plan 0023 Milestone 8: pbr_material_demo_scene likewise.
add_dependencies(atlantis_image_regression_gpu_tests ${ATLANTIS_minimal_cube_TARGET} ${ATLANTIS_world_scene_TARGET}
  ${ATLANTIS_textured_quad_unorm_TARGET} ${ATLANTIS_textured_quad_srgb_TARGET}
  ${ATLANTIS_textured_quad_left_TARGET} ${ATLANTIS_textured_quad_right_TARGET}
  ${ATLANTIS_material_demo_scene_TARGET} ${ATLANTIS_lighting_demo_scene_TARGET}
  ${ATLANTIS_pbr_material_demo_scene_TARGET} ${ATLANTIS_hdr_roll_off_demo_scene_TARGET})

# Same build-tree shader artifact set as the golden generator -- this
# executable renders the identical fixture. WORKING_DIRECTORY above
# stays scoped to shader loading only, matching this repository's
# existing, already-established convention -- deliberately not extended
# to the three paths above, which use target_compile_definitions()
# instead precisely because they must resolve identically regardless of
# which directory ctest (or a human) happens to invoke this executable
# from.
add_dependencies(atlantis_image_regression_gpu_tests minimal_mesh_shaders textured_quad_shaders lit_textured_shaders
  pbr_direct_lit_shaders output_transform_unorm_shaders)
add_custom_command(TARGET atlantis_image_regression_gpu_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.vert.spv"
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.vert.refl.json"
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.frag.spv"
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders/"
  # Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): the output-transform-
  # unorm shader pair -- needed by minimal_cube_fixture.cpp/
  # world_scene_fixture.cpp/world_scene_loaded_fixture.cpp/
  # textured_quad_fixture.cpp, which all follow this same WORKING_DIRECTORY-
  # relative "shaders/<name>.{vert,frag}.spv" convention (never
  # BootstrapConfig) -- mirrors the two copy commands above exactly.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders/"
)
