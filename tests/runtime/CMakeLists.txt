# Plan 0013 Step 1: GPU-independent tests only -- no Device, no GPU, and
# no real window required to run.
add_executable(atlantis_runtime_tests
  lifecycle_state_tests.cpp
  bootstrap_config_tests.cpp
  environment_realization_tests.cpp
  exit_reason_tests.cpp
  error_classification_tests.cpp
  init_error_tests.cpp
  runtime_ownership_tests.cpp
  scene_extraction_tests.cpp
  scene_manifest_tests.cpp
  scene_load_tests.cpp
  pbr_reflection_cross_check_tests.cpp
)

target_link_libraries(atlantis_runtime_tests
  PRIVATE
    Atlantis::RuntimeHost
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# Plan 0023 Milestone 7: pbr_reflection_cross_check_tests.cpp's own
# GPU-independent cross-check needs (1) the already-built, always-fresh
# pbr_direct_lit.vert/frag.refl.json artifacts (ATLANTIS's own
# transformed schema -- for the PbrPushConstants push-constant-range
# check) and (2) a real slangc invocation of its own against the
# CURRENT pbr_direct_lit.slang source (for the 320-byte camera-buffer
# check, whose own field-level struct layout ATLANTIS's transformed
# schema does not retain, ADR-0030's own scope) -- never a stale,
# committed golden JSON for either.
target_compile_definitions(atlantis_runtime_tests PRIVATE
  ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_SLANGC_EXECUTABLE="${ATLANTIS_SLANGC_EXECUTABLE}"
  ATLANTIS_PBR_DIRECT_LIT_SLANG_SOURCE="${CMAKE_SOURCE_DIR}/shaders/pbr_direct_lit/pbr_direct_lit.slang"
  ATLANTIS_PBR_IBL_SLANG_SOURCE="${CMAKE_SOURCE_DIR}/shaders/pbr_ibl/pbr_ibl.slang"
)
add_dependencies(atlantis_runtime_tests pbr_direct_lit_shaders pbr_ibl_shaders)

catch_discover_tests(atlantis_runtime_tests DISCOVERY_MODE PRE_TEST)

# Plan 0013 Step 5 / Section D10: the real windowed smoke test -- links
# Atlantis::RuntimeHost directly, never atlantis_runtime. Plan 0018 PR #88
# final review round: material_realization_gpu_tests.cpp is headless (an
# OffscreenTarget, no window) but shares this target for its own
# ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR/textured_quad_shaders wiring,
# already declared below.
add_executable(atlantis_runtime_gpu_tests
  runtime_smoke_gpu_tests.cpp
  material_realization_gpu_tests.cpp
  environment_realization_gpu_tests.cpp
  material_ibl_selection_gpu_tests.cpp
  pbr_render_gpu_tests.cpp
)

target_link_libraries(atlantis_runtime_gpu_tests
  PRIVATE
    Atlantis::RuntimeHost
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# Same build-tree path pattern as src/runtime/CMakeLists.txt's own
# atlantis_runtime target (Plan 0013 Section D9) -- absolute,
# configuration-independent, no working-directory dependence.
target_compile_definitions(atlantis_runtime_gpu_tests PRIVATE
  ATLANTIS_RUNTIME_SHADER_DIR="${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_ASSET_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}"
  ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH="${ATLANTIS_world_scene_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_SCENE_METADATA_PATH="${ATLANTIS_world_scene_METADATA_PATH}"
  ATLANTIS_RUNTIME_SCENE_MANIFEST_PATH="${ATLANTIS_world_scene_MANIFEST_PATH}"
  # Plan 0018 Section P10: the second, MaterialKind::UnlitTextured
  # built-in shader pair -- mirrors ATLANTIS_RUNTIME_SHADER_DIR's own
  # sourcing exactly, now that shaders/textured_quad is unconditional.
  ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  # Plan 0019 Section P6: the third, MaterialKind::LitTextured built-in
  # shader pair -- material_realization_gpu_tests.cpp's own widened
  # realizePendingMaterials()/rebuildMaterialsForFormatChange() calls
  # need a real litTextured* trio, even where no test in this file
  # realizes one.
  ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  # Plan 0023 Milestone 5: the fourth, MaterialKind::PbrDirectLit
  # built-in shader pair -- material_realization_gpu_tests.cpp's own
  # further-widened realizePendingMaterials()/rebuildMaterialsForFormatChange()
  # calls need a real pbrDirectLit* trio too, even where no test in this
  # file realizes one.
  ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR="${ATLANTIS_pbr_ibl_SHADER_OUTPUT_DIR}"
  # Plan 0026 Milestone 6 (ADR-0071): material_realization_gpu_tests.cpp's
  # own new N=6-with-sky descriptor-pool TEST_CASE needs the sky shader
  # pair's own output directory, mirroring
  # ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR's own sourcing exactly.
  ATLANTIS_RUNTIME_SKY_SHADER_DIR="${ATLANTIS_sky_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH="${ATLANTIS_ibl_studio_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH="${ATLANTIS_ibl_studio_METADATA_PATH}"
  # Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): pbr_render_gpu_tests.cpp's
  # own renderOneFrame() helper needs the output-transform-unorm shader
  # pair (its own kColorFormat is fixed Rgba8Unorm), mirroring
  # ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR's own sourcing exactly.
  ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  # runtime_smoke_gpu_tests.cpp's own real RuntimeApplication needs both
  # output-transform variants, mirroring main.cpp's own identical need.
  ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR="${ATLANTIS_output_transform_srgb_SHADER_OUTPUT_DIR}"
  # Plan 0027 Milestone 7 (ADR-0072 D-7): material_realization_gpu_tests.cpp's
  # own new N=6-with-shadow-cast descriptor-pool TEST_CASE needs the
  # shadow_cast shader pair's own output directory, mirroring
  # ATLANTIS_RUNTIME_SKY_SHADER_DIR's own sourcing exactly.
  ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"
)

add_dependencies(atlantis_runtime_gpu_tests minimal_mesh_shaders textured_quad_shaders lit_textured_shaders
  pbr_direct_lit_shaders pbr_ibl_shaders output_transform_unorm_shaders output_transform_srgb_shaders sky_shaders
  shadow_cast_shaders
  ${ATLANTIS_minimal_cube_TARGET} ${ATLANTIS_world_scene_TARGET} ${ATLANTIS_ibl_studio_TARGET})

# LABELS "gpu": the existing plain ctest convention Spec 0006/Spec 0010
# already established -- matching tests/image_regression/CMakeLists.txt's
# own identical registration pattern.
catch_discover_tests(atlantis_runtime_gpu_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "gpu"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_runtime_gpu_tests>"
)
