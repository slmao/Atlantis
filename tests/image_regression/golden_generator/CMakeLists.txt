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
add_dependencies(atlantis_image_regression_golden_generator minimal_mesh_shaders)
add_custom_command(TARGET atlantis_image_regression_golden_generator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders/"
)

# Deliberately no catch_discover_tests()/LABELS "gpu"/add_test() call in
# this file -- the concrete enforcement of "never registered with
# CTest." Deliberately no run_... convenience custom target: this tool
# requires a positional argument that varies per invocation.
