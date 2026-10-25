# Plan 0047 M5 (P15): every scene-loading test resolves its scene through the one assembled build catalog;
# the per-scene ATLANTIS_*_GUID definitions below come from assets/asset_catalog.txt. The catalog target
# is built by ALL, so it exists before any test runs.
add_compile_definitions(ATLANTIS_ASSET_CATALOG_PATH="${ATLANTIS_ASSET_CATALOG_PATH}")

# Plan 0013 Step 1: GPU-independent tests only -- no Device, no GPU, and
# no real window required to run.
# Plan 0032 M1: cli.cpp is atlantis_runtime's own private, executable-
# scoped startup layer (src/runtime/cli.h, not part of
# Atlantis::RuntimeHost -- ADR-0076 Decision 2) -- included here via a
# relative-path source reference, the same "test compiles a module's
# private source file directly" precedent
# tests/shader_system/json_parser_tests.cpp already establishes,
# recompiled as its own translation unit into this target (cli.cpp has
# no home library to link against). cli_tests.cpp is the new test file
# exercising it. No change to atlantis_runtime_gpu_tests below.
add_executable(atlantis_runtime_tests
  lifecycle_state_tests.cpp
  bootstrap_config_tests.cpp
  environment_realization_tests.cpp
  exit_reason_tests.cpp
  error_classification_tests.cpp
  init_error_tests.cpp
  runtime_ownership_tests.cpp
  scene_extraction_tests.cpp
  scene_load_tests.cpp
  entity_ref_resolution_tests.cpp
  frame_input_equivalence_tests.cpp
  scene_collection_tests.cpp
  runtime_world_access_tests.cpp
  pbr_reflection_cross_check_tests.cpp
  material_realization_tests.cpp
  cli_tests.cpp
  runtime_control_tests.cpp
  ../../src/runtime/cli.cpp
  # Plan 0059 M9 (ADR-0117): the benchmark's GPU-independent parts, compiled here as
  # atlantis_runtime compiles them -- the cli.cpp precedent.
  ../../src/runtime/benchmark_report.cpp
  ../../src/runtime/benchmark_loop.cpp
  benchmark_report_tests.cpp
  benchmark_loop_tests.cpp
  # Plan 0060 (ADR-0119): the performance gate's GPU-independent parts, compiled here as
  # atlantis_runtime compiles them.
  ../../src/runtime/gate_canary.cpp
  ../../src/runtime/gate_set.cpp
  ../../src/runtime/gate_policy.cpp
  ../../src/runtime/gate_evaluate.cpp
  ../../src/runtime/gate_orchestrate.cpp
  ../../src/runtime/gate_process_windows.cpp
  ../../src/runtime/gate_store.cpp
  ../../src/runtime/gate_applies.cpp
  ../../src/runtime/gate_run.cpp
  gate_set_tests.cpp
  gate_policy_tests.cpp
  gate_evaluate_tests.cpp
  gate_orchestrate_tests.cpp
  gate_run_tests.cpp
  gate_store_tests.cpp
  gate_record_tests.cpp
  gate_applies_tests.cpp
  gate_canary_tests.cpp
  point_light_rejection_equivalence_tests.cpp
  # Plan 0062 M1 (ADR-0123 D1): the editor's copy of the camera convention,
  # held to extractCameraMatrices().
  editor_camera_convention_tests.cpp
  # Plan 0062 M3 (ADR-0123 D2): the pick services, GPU-free.
  scene_picking_tests.cpp
)

# Plan 0062 M1: Atlantis::Editor is linked by this test executable only, for
# the camera convention cross-check; atlantis_runtime_host never links it.
target_link_libraries(atlantis_runtime_tests
  PRIVATE
    Atlantis::RuntimeHost
    Atlantis::Editor
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
  # Plan 0055 M4 (P7): the commandsHeld_ writer scan.
  ATLANTIS_RUNTIME_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/runtime"
  ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_SLANGC_EXECUTABLE="${ATLANTIS_SLANGC_EXECUTABLE}"
  ATLANTIS_PBR_DIRECT_LIT_SLANG_SOURCE="${CMAKE_SOURCE_DIR}/shaders/pbr_direct_lit/pbr_direct_lit.slang"
  ATLANTIS_PBR_IBL_SLANG_SOURCE="${CMAKE_SOURCE_DIR}/shaders/pbr_ibl/pbr_ibl.slang"
  # Plan 0040 Milestone 2 (Spec 0040 ruling O4): the 11/11 CameraUniform
  # cross-check reflects every declaring shader from its own source.
  ATLANTIS_SHADER_SOURCE_ROOT="${CMAKE_SOURCE_DIR}/shaders"
)
add_dependencies(atlantis_runtime_tests pbr_direct_lit_shaders pbr_ibl_shaders)

# Plan 0051 P7 (Spec 0051 ruling Q7 V2, J4): the frame-input equivalence test
# decodes every scene of the assembled build catalog, and checks the four
# Runtime whitelist scenes are among them; Bistro joins when its content build
# step exists (content-gated, as atlantis_runtime is).
target_compile_definitions(atlantis_runtime_tests PRIVATE
  ATLANTIS_WHITELIST_INTEGRATED_SHOWCASE_SCENE_GUID="${ATLANTIS_integrated_showcase_demo_scene_GUID}"
  ATLANTIS_WHITELIST_IBL_MATERIAL_DEMO_SCENE_GUID="${ATLANTIS_ibl_material_demo_scene_GUID}"
  ATLANTIS_WHITELIST_PBR_NORMAL_MAP_DEMO_SCENE_GUID="${ATLANTIS_pbr_normal_map_demo_scene_GUID}"
  ATLANTIS_WHITELIST_PBR_MATERIALS_SHOWCASE_SCENE_GUID="${ATLANTIS_pbr_materials_showcase_scene_GUID}"
)
# Plan 0060 M2: the gate tests read the committed policy file (and, from M6, the committed
# baseline store) under tests/performance/.
target_compile_definitions(atlantis_runtime_tests PRIVATE
  ATLANTIS_PERFORMANCE_DIR="${CMAKE_SOURCE_DIR}/tests/performance"
  ATLANTIS_GATE_ALL_TIERS_POLICY="${CMAKE_SOURCE_DIR}/tests/runtime/data/gate_policy_all_tiers.json"
)
# Plan 0061 M2 (P5): the equivalence test reads Bistro's point lights from the committed overlay
# (lights only -- no content/bistro needed).
target_compile_definitions(atlantis_runtime_tests PRIVATE
  ATLANTIS_BISTRO_OVERLAY_SCENE_PATH="${CMAKE_SOURCE_DIR}/assets/bistro/bistro_overlay.scene.txt"
)
add_dependencies(atlantis_runtime_tests atlantis_asset_catalog)
if(DEFINED ATLANTIS_bistro_scene_TARGET)
  target_compile_definitions(atlantis_runtime_tests PRIVATE
    ATLANTIS_BISTRO_IMPORT_GUID="${ATLANTIS_bistro_scene_IMPORT_GUID}"
  )
  add_dependencies(atlantis_runtime_tests ${ATLANTIS_bistro_scene_TARGET})
endif()

catch_discover_tests(atlantis_runtime_tests DISCOVERY_MODE PRE_TEST)

# Plan 0013 Step 5 / Section D10: the real windowed smoke test -- links
# Atlantis::RuntimeHost directly, never atlantis_runtime. Plan 0018 PR #88
# final review round: material_realization_gpu_tests.cpp is headless (an
# OffscreenTarget, no window) but shares this target for its own
# ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR/textured_quad_shaders wiring,
# already declared below.
add_executable(atlantis_runtime_gpu_tests
  runtime_smoke_gpu_tests.cpp
  # Plan 0056 M6 (J4): the executable-private editor adapter, compiled here as
  # atlantis_runtime compiles it -- the cli.cpp precedent.
  ../../src/runtime/editor_attachment.cpp
  material_realization_gpu_tests.cpp
  environment_realization_gpu_tests.cpp
  material_ibl_selection_gpu_tests.cpp
  pbr_render_gpu_tests.cpp
  # Plan 0057 M5 (P7): the Gameplay SDK against a real Runtime -- the North
  # star, Bistro, batch equivalence and read isolation, fatal VVL.
  gameplay_sdk_gpu_tests.cpp
  # Plan 0059 M9 (ADR-0117): runtime_smoke_gpu_tests.cpp's variant and whole-run tests,
  # with the benchmark sources compiled as atlantis_runtime compiles them.
  ../../src/runtime/cli.cpp
  ../../src/runtime/benchmark_report.cpp
  ../../src/runtime/benchmark_loop.cpp
  ../../src/runtime/benchmark_variants.cpp
  ../../src/runtime/benchmark_run.cpp
  # Plan 0060 M4 (ADR-0119 D1): the gate's real child-process launcher and CPU sampler.
  ../../src/runtime/gate_canary.cpp
  ../../src/runtime/gate_orchestrate.cpp
  ../../src/runtime/gate_process_windows.cpp
  ../../src/runtime/gate_set.cpp
  ../../src/runtime/gate_store.cpp
  gate_process_gpu_tests.cpp
  # Plan 0064 M1 (ADR-0126 D1-D3): the real executable as a child of another process's window.
  child_window_gpu_tests.cpp
  # Plan 0065 M4: the scene reload (ADR-0131).
  scene_reload_gpu_tests.cpp
)

target_link_libraries(atlantis_runtime_gpu_tests
  PRIVATE
    Atlantis::RuntimeHost
    # Plan 0054 M5: the north-star test drives the CLI client over a
    # connection to the running application.
    Atlantis::Cli
    # Plan 0055 M5: the capture tests decode the PNGs a capture writes.
    Atlantis::ImageRegressionSupport
    # Plan 0055 M7 (ruling Q7, A-a): the in-process north star attaches over a
    # real loopback socket, both halves in this one process.
    Atlantis::RemoteClient
    Atlantis::RemoteServer
    # Plan 0056 M6/M7: the editor attached to the frame, through its adapter.
    Atlantis::Editor
    # Plan 0057 M5: the Gameplay SDK as a client of the running application.
    Atlantis::GameplaySdk
    Atlantis::Authoring  # Plan 0065 M4: the reload tests' authoring host
    # Plan 0059 M5: the smoke test reads the backend's debug-name observer.
    Vulkan::Vulkan
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# Same build-tree path pattern as src/runtime/CMakeLists.txt's own
# atlantis_runtime target (Plan 0013 Section D9) -- absolute,
# configuration-independent, no working-directory dependence.
target_include_directories(atlantis_runtime_gpu_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src)

if(WIN32)
  # Plan 0059 M9: the process-level benchmark test runs the real executable.
  target_compile_definitions(atlantis_runtime_gpu_tests PRIVATE ATLANTIS_RUNTIME_EXECUTABLE="$<TARGET_FILE:atlantis_runtime>"
    # Plan 0060 M5: the gate's process-level tests read the committed policy file.
    ATLANTIS_PERFORMANCE_DIR="${CMAKE_SOURCE_DIR}/tests/performance"
    ATLANTIS_GATE_ALL_TIERS_POLICY="${CMAKE_SOURCE_DIR}/tests/runtime/data/gate_policy_all_tiers.json")
  add_dependencies(atlantis_runtime_gpu_tests atlantis_runtime)
endif()

target_compile_definitions(atlantis_runtime_gpu_tests PRIVATE
  ATLANTIS_RUNTIME_SHADER_DIR="${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_ASSET_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}"
  # Plan 0028 Milestone 2: this target's own real windowed smoke lifecycle
  # switches from world_scene to integrated_showcase_demo_scene, matching
  # atlantis_runtime's own default -- a separate, independent switch from
  # src/runtime/CMakeLists.txt's own (this target does not inherit it).
  ATLANTIS_RUNTIME_SCENE_GUID="${ATLANTIS_integrated_showcase_demo_scene_GUID}"
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
  # Plan 0029 Section P13: pbr_render_gpu_tests.cpp's own new binding-5
  # GPU test needs the pbr_ibl_normal_map shader pair's own output
  # directory, mirroring ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR's own
  # sourcing exactly.
  ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR="${ATLANTIS_pbr_ibl_normal_map_SHADER_OUTPUT_DIR}"
  # Plan 0029 Section P15: material_realization_gpu_tests.cpp's own new
  # normal-map upload-ordering TEST_CASE needs the pbr_direct_lit_normal_map
  # shader pair's own output directory, mirroring
  # ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR's own sourcing exactly.
  ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR="${ATLANTIS_pbr_direct_lit_normal_map_SHADER_OUTPUT_DIR}"
  # Plan 0044 Milestone 1: runtime_smoke_gpu_tests.cpp creates the Runtime's
  # bloom Pipelines, as main.cpp configures them.
  ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR="${ATLANTIS_bloom_downsample_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR="${ATLANTIS_bloom_upsample_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR="${ATLANTIS_bloom_composite_SHADER_OUTPUT_DIR}"
  # Plan 0044 Milestone 2: the bloom smoke TEST_CASE's scene.
  ATLANTIS_RUNTIME_BLOOM_SCENE_GUID="${ATLANTIS_bloom_demo_scene_GUID}"
  ATLANTIS_RUNTIME_EDITOR_UI_SHADER_DIR="${ATLANTIS_editor_ui_SHADER_OUTPUT_DIR}"
  # Plan 0066 M4: viewport_overlay_gpu_tests.cpp enables the Viewport overlay as main.cpp does.
  ATLANTIS_RUNTIME_VIEWPORT_OVERLAY_SHADER_DIR="${ATLANTIS_viewport_overlay_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_SELECTION_MASK_SHADER_DIR="${ATLANTIS_selection_mask_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_SELECTION_OUTLINE_SHADER_DIR="${ATLANTIS_selection_outline_SHADER_OUTPUT_DIR}"
  # Plan 0065 M4: the committed scene sources the authoring session opens, and
  # the build tree the reload fixture's catalog shares with the build's.
  ATLANTIS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  ATLANTIS_BUILD_DIR="${CMAKE_BINARY_DIR}"
)

add_dependencies(atlantis_runtime_gpu_tests minimal_mesh_shaders textured_quad_shaders lit_textured_shaders
  pbr_direct_lit_shaders pbr_ibl_shaders output_transform_unorm_shaders output_transform_srgb_shaders sky_shaders
  shadow_cast_shaders pbr_direct_lit_normal_map_shaders pbr_ibl_normal_map_shaders
  bloom_downsample_shaders bloom_upsample_shaders bloom_composite_shaders editor_ui_shaders
  viewport_overlay_shaders selection_mask_shaders selection_outline_shaders
  ${ATLANTIS_minimal_cube_TARGET} ${ATLANTIS_integrated_showcase_demo_scene_TARGET} ${ATLANTIS_ground_plane_TARGET}
  ${ATLANTIS_ibl_studio_TARGET} ${ATLANTIS_bloom_demo_scene_TARGET})

# LABELS "gpu": the existing plain ctest convention Spec 0006/Spec 0010
# already established -- matching tests/image_regression/CMakeLists.txt's
# own identical registration pattern.
catch_discover_tests(atlantis_runtime_gpu_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "gpu"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_runtime_gpu_tests>"
)

# Plan 0055 M7 (Spec 0055 R8): the Bistro north star is content-gated, as
# atlantis_runtime's `bistro` entry is.
if(DEFINED ATLANTIS_bistro_scene_TARGET)
  target_compile_definitions(atlantis_runtime_gpu_tests PRIVATE
    ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID="${ATLANTIS_bistro_scene_IMPORT_GUID}"
    ATLANTIS_RUNTIME_BISTRO_SCENE_SOURCE="${ATLANTIS_bistro_scene_IMPORT_DIR}/bistro/bistro.scene.txt"
  )
  add_dependencies(atlantis_runtime_gpu_tests ${ATLANTIS_bistro_scene_TARGET})
endif()

# Plan 0065 M4 (ADR-0131 D6): a reload equals a fresh start on the same
# document. A windowed RuntimeApplication is one per process, so the fresh
# start is its own hidden test, run first as a CTest fixture, and the reload
# test compares against what it recorded.
add_test(NAME "scene reload fixture: a fresh start on the edited default scene"
  COMMAND atlantis_runtime_gpu_tests "[.reload_fresh]"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_runtime_gpu_tests>")
set_tests_properties("scene reload fixture: a fresh start on the edited default scene"
  PROPERTIES FIXTURES_SETUP scene_reload_fresh LABELS "gpu")
add_test(NAME "scene reload: a reload of an edited document equals a fresh start on it; applying back releases"
  COMMAND atlantis_runtime_gpu_tests "[.reload_compare]"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_runtime_gpu_tests>")
set_tests_properties("scene reload: a reload of an edited document equals a fresh start on it; applying back releases"
  PROPERTIES FIXTURES_REQUIRED scene_reload_fresh LABELS "gpu")
