# Plan 0013 Step 1: GPU-independent tests only -- no Device, no GPU, and
# no real window required to run.
add_executable(atlantis_runtime_tests
  lifecycle_state_tests.cpp
  exit_reason_tests.cpp
  error_classification_tests.cpp
  init_error_tests.cpp
  runtime_ownership_tests.cpp
  scene_extraction_tests.cpp
  scene_manifest_tests.cpp
  scene_load_tests.cpp
)

target_link_libraries(atlantis_runtime_tests
  PRIVATE
    Atlantis::RuntimeHost
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_runtime_tests DISCOVERY_MODE PRE_TEST)

# Plan 0013 Step 5 / Section D10: the real windowed smoke test -- links
# Atlantis::RuntimeHost directly, never atlantis_runtime.
add_executable(atlantis_runtime_gpu_tests
  runtime_smoke_gpu_tests.cpp
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
)

add_dependencies(atlantis_runtime_gpu_tests minimal_mesh_shaders ${ATLANTIS_minimal_cube_TARGET})

# LABELS "gpu": the existing plain ctest convention Spec 0006/Spec 0010
# already established -- matching tests/image_regression/CMakeLists.txt's
# own identical registration pattern.
catch_discover_tests(atlantis_runtime_gpu_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "gpu"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_runtime_gpu_tests>"
)
