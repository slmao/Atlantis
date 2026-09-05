add_executable(atlantis_image_regression_golden_generator main.cpp)

target_link_libraries(atlantis_image_regression_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    atlantis_compiler_warnings
)

# Absolute, configure-time-computed paths, immune to whatever directory
# this tool happens to be invoked from -- never a CWD-relative path,
# never a directory search. The same two definitions are applied to
# atlantis_image_regression_gpu_tests, so both consumers resolve
# identically.
target_compile_definitions(atlantis_image_regression_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
)

# Same build-tree Shader-System-produced artifact set every prior
# headless/windowed demo/test copies -- this tool renders the identical
# minimal_mesh shader pair. Shader loading (unlike the two paths above)
# keeps this repository's existing, already-established
# WORKING_DIRECTORY + relative-path convention unchanged.
add_dependencies(atlantis_image_regression_golden_generator minimal_mesh_shaders output_transform_unorm_shaders
  shadow_cast_shaders)
add_custom_command(TARGET atlantis_image_regression_golden_generator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.refl.json"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders/"
)

# Plan 0014 Section D10: a second, standalone golden-generator
# executable -- not a change to the existing target above -- reusing the
# identical Atlantis::ImageRegressionSupport-provided git/provenance/PNG
# steps, with only "render one frame" replaced.
add_executable(atlantis_image_regression_world_scene_golden_generator world_scene_main.cpp)

target_link_libraries(atlantis_image_regression_world_scene_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    Atlantis::World
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_world_scene_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
)

add_dependencies(atlantis_image_regression_world_scene_golden_generator minimal_mesh_shaders output_transform_unorm_shaders
  shadow_cast_shaders ${ATLANTIS_minimal_cube_TARGET})
add_custom_command(TARGET atlantis_image_regression_world_scene_golden_generator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_image_regression_world_scene_golden_generator>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_world_scene_golden_generator>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_world_scene_golden_generator>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.refl.json"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_world_scene_golden_generator>/shaders/"
)

# Plan 0016 Milestone 9: a third, standalone golden-generator executable
# for the textured quad fixture -- same reused git/provenance/PNG steps,
# "render one frame" replaced with renderTexturedQuadFrame().
add_executable(atlantis_image_regression_textured_quad_golden_generator textured_quad_main.cpp)

target_link_libraries(atlantis_image_regression_textured_quad_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_textured_quad_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
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
)

add_dependencies(atlantis_image_regression_textured_quad_golden_generator textured_quad_shaders
  output_transform_unorm_shaders shadow_cast_shaders
  ${ATLANTIS_textured_quad_unorm_TARGET} ${ATLANTIS_textured_quad_srgb_TARGET}
  ${ATLANTIS_textured_quad_left_TARGET} ${ATLANTIS_textured_quad_right_TARGET})
add_custom_command(TARGET atlantis_image_regression_textured_quad_golden_generator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_image_regression_textured_quad_golden_generator>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.vert.spv"
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.vert.refl.json"
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.frag.spv"
      "${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}/textured_quad.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_textured_quad_golden_generator>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.vert.refl.json"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.spv"
      "${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}/output_transform_unorm.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_textured_quad_golden_generator>/shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.vert.refl.json"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.spv"
      "${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}/shadow_cast.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_textured_quad_golden_generator>/shaders/"
)

# Plan 0018 Milestone 16: a fourth, standalone golden-generator executable
# for the material_demo fixture -- same reused git/provenance/PNG steps,
# "render one frame" replaced with renderMaterialDemoFrame(). Unlike the
# three generators above, this one links Atlantis::RuntimeHost (via
# Atlantis::ImageRegressionFixture's own PUBLIC dependency, Spec 0018
# D12's sanctioned exception) to build the BootstrapConfig
# setUpMaterialDemoFixture() requires.
add_executable(atlantis_image_regression_material_demo_golden_generator material_demo_main.cpp)

target_link_libraries(atlantis_image_regression_material_demo_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_material_demo_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  ATLANTIS_material_demo_scene_ARTIFACT_PATH="${ATLANTIS_material_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_material_demo_scene_METADATA_PATH="${ATLANTIS_material_demo_scene_METADATA_PATH}"
  ATLANTIS_material_demo_scene_MANIFEST_PATH="${ATLANTIS_material_demo_scene_MANIFEST_PATH}"
  ATLANTIS_MATERIAL_DEMO_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  # Plan 0019 Section P6: see tests/image_regression/CMakeLists.txt's own
  # identical comment on the gpu_tests target.
  ATLANTIS_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  # Plan 0023 Milestone 5: see tests/image_regression/CMakeLists.txt's
  # own identical comment on the gpu_tests target.
  ATLANTIS_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  # Plan 0027 Milestone 9 (ADR-0072 D-1/P9f): see
  # tests/image_regression/CMakeLists.txt's own identical comment on the
  # gpu_tests target.
  ATLANTIS_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"
)

add_dependencies(atlantis_image_regression_material_demo_golden_generator textured_quad_shaders lit_textured_shaders
  pbr_direct_lit_shaders output_transform_unorm_shaders shadow_cast_shaders ${ATLANTIS_material_demo_scene_TARGET})

# Plan 0019 Section P10/Milestone 10b: a fifth, standalone golden-
# generator executable for the lighting_demo fixture -- same reused
# git/provenance/PNG steps, "render one frame" replaced with
# renderLightingDemoFrame().
add_executable(atlantis_image_regression_lighting_demo_golden_generator lighting_demo_main.cpp)

target_link_libraries(atlantis_image_regression_lighting_demo_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_lighting_demo_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  ATLANTIS_lighting_demo_scene_ARTIFACT_PATH="${ATLANTIS_lighting_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_lighting_demo_scene_METADATA_PATH="${ATLANTIS_lighting_demo_scene_METADATA_PATH}"
  ATLANTIS_lighting_demo_scene_MANIFEST_PATH="${ATLANTIS_lighting_demo_scene_MANIFEST_PATH}"
  ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  # Plan 0023 Milestone 5: see tests/image_regression/CMakeLists.txt's
  # own identical comment on the gpu_tests target.
  ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_LIGHTING_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"
)

add_dependencies(atlantis_image_regression_lighting_demo_golden_generator textured_quad_shaders lit_textured_shaders
  pbr_direct_lit_shaders output_transform_unorm_shaders shadow_cast_shaders ${ATLANTIS_lighting_demo_scene_TARGET})

# Plan 0023 Milestone 8: a sixth, standalone golden-generator executable
# for the pbr_material_demo fixture -- same reused git/provenance/PNG
# steps, "render one frame" replaced with renderPbrMaterialDemoFrame().
add_executable(atlantis_image_regression_pbr_material_demo_golden_generator pbr_material_demo_main.cpp)

target_link_libraries(atlantis_image_regression_pbr_material_demo_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_pbr_material_demo_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH="${ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_pbr_material_demo_scene_METADATA_PATH="${ATLANTIS_pbr_material_demo_scene_METADATA_PATH}"
  ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH="${ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH}"
  ATLANTIS_PBR_MATERIAL_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_PBR_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  ATLANTIS_PBR_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_PBR_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"
)

add_dependencies(atlantis_image_regression_pbr_material_demo_golden_generator textured_quad_shaders
  lit_textured_shaders pbr_direct_lit_shaders output_transform_unorm_shaders shadow_cast_shaders
  ${ATLANTIS_pbr_material_demo_scene_TARGET})

# Plan 0025/M9: a separate executable reusing the PBR generator's guarded
# capture/provenance implementation while selecting the IBL fixture/config.
add_executable(atlantis_image_regression_ibl_material_demo_golden_generator pbr_material_demo_main.cpp)
target_link_libraries(atlantis_image_regression_ibl_material_demo_golden_generator PRIVATE
  Atlantis::ImageRegressionFixture Atlantis::ImageRegressionSupport atlantis_compiler_warnings)
target_compile_definitions(atlantis_image_regression_ibl_material_demo_golden_generator PRIVATE
  ATLANTIS_IBL_GOLDEN_GENERATOR=1
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH="${ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_ibl_material_demo_scene_METADATA_PATH="${ATLANTIS_ibl_material_demo_scene_METADATA_PATH}"
  ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH="${ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH}"
  ATLANTIS_IBL_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_IBL_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  ATLANTIS_IBL_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_IBL_DEMO_PBR_IBL_SHADER_DIR="${ATLANTIS_pbr_ibl_SHADER_OUTPUT_DIR}"
  # Plan 0026 Milestone 7 (ADR-0071): the sky shader pair's own output
  # directory, mirroring ATLANTIS_IBL_DEMO_PBR_IBL_SHADER_DIR's own
  # identical need.
  ATLANTIS_IBL_DEMO_SKY_SHADER_DIR="${ATLANTIS_sky_SHADER_OUTPUT_DIR}"
  ATLANTIS_IBL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  ATLANTIS_IBL_DEMO_ENVIRONMENT_ARTIFACT_PATH="${ATLANTIS_ibl_studio_ARTIFACT_PATH}"
  ATLANTIS_IBL_DEMO_ENVIRONMENT_METADATA_PATH="${ATLANTIS_ibl_studio_METADATA_PATH}"
  ATLANTIS_IBL_DEMO_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"
  ATLANTIS_IBL_DEMO_SOURCE_SHA256="a5d9b1a50102f8f2761a61be0574b5036277a8da61268c922d3f82ebe839e4e4"
  ATLANTIS_IBL_DEMO_ARTIFACT_SHA256="bd0c9f75c5e26418f7de6f9165675fb54a5a045cdbb3265aae05ba18be4a1f83"
)
add_dependencies(atlantis_image_regression_ibl_material_demo_golden_generator textured_quad_shaders
  lit_textured_shaders pbr_direct_lit_shaders pbr_ibl_shaders output_transform_unorm_shaders sky_shaders
  shadow_cast_shaders ${ATLANTIS_ibl_material_demo_scene_TARGET} ${ATLANTIS_ibl_studio_TARGET})

# Plan 0024 Milestone 10: a seventh standalone generator for the
# focused HDR roll-off baseline, using the same fixture/render path as
# pbr_material_demo with its own over-range authored scene.
add_executable(atlantis_image_regression_hdr_roll_off_demo_golden_generator hdr_roll_off_demo_main.cpp)

target_link_libraries(atlantis_image_regression_hdr_roll_off_demo_golden_generator
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_hdr_roll_off_demo_golden_generator PRIVATE
  ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
  ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH="${ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH="${ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH}"
  ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH="${ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_LIT_TEXTURED_SHADER_DIR="${ATLANTIS_lit_textured_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_PBR_DIRECT_LIT_SHADER_DIR="${ATLANTIS_pbr_direct_lit_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR="${ATLANTIS_output_transform_unorm_SHADER_OUTPUT_DIR}"
  ATLANTIS_HDR_ROLL_OFF_DEMO_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"
)

add_dependencies(atlantis_image_regression_hdr_roll_off_demo_golden_generator textured_quad_shaders
  lit_textured_shaders pbr_direct_lit_shaders output_transform_unorm_shaders shadow_cast_shaders
  ${ATLANTIS_hdr_roll_off_demo_scene_TARGET})

# Deliberately no catch_discover_tests()/LABELS "gpu"/add_test() call in
# this file -- the concrete enforcement of "never registered with
# CTest." Deliberately no run_... convenience custom target: this tool
# requires a positional argument that varies per invocation.
