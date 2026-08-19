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
)

target_link_libraries(atlantis_image_regression_gpu_tests
  PRIVATE
    Atlantis::ImageRegressionFixture
    Atlantis::ImageRegressionSupport
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
)

# Plan 0012 Step 6: the asset-sourced GPU test case needs
# minimal_cube's own cooked artifact/metadata to already exist.
add_dependencies(atlantis_image_regression_gpu_tests ${ATLANTIS_minimal_cube_TARGET})

# Same build-tree shader artifact set as the golden generator -- this
# executable renders the identical fixture. WORKING_DIRECTORY above
# stays scoped to shader loading only, matching this repository's
# existing, already-established convention -- deliberately not extended
# to the three paths above, which use target_compile_definitions()
# instead precisely because they must resolve identically regardless of
# which directory ctest (or a human) happens to invoke this executable
# from.
add_dependencies(atlantis_image_regression_gpu_tests minimal_mesh_shaders)
add_custom_command(TARGET atlantis_image_regression_gpu_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
      "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
      "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders/"
)
