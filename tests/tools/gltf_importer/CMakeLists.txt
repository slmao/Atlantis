# Plan 0037 Milestone 3: GPU-independent tests for atlantis_gltf_importer_lib
# (tiny in-memory glTF files, no fixtures), plus the real-Bistro census smoke
# test, which runs only when the fetched content exists and is labelled
# "content" (Spec 0037 content policy: SKIP, never FAIL, when absent).
add_executable(atlantis_gltf_importer_tests
  gltf_import_tests.cpp
  gltf_material_tests.cpp
  gltf_scene_tests.cpp
  bistro_census_tests.cpp
  bistro_end_to_end_tests.cpp
  # Plan 0046 Milestone 2: the cooker's cook-manifest mode over a synthetic import.
  cook_manifest_mode_tests.cpp
  # Plan 0047 M3: derived identity, the import-root lookup, and the real
  # importer run twice.
  import_identity_tests.cpp
  module_boundary_tests.cpp
)

# Cgltf::Cgltf is header-only here: the census test reads the real file
# through cgltf directly, and its implementation symbols come from the
# importer library's single CGLTF_IMPLEMENTATION translation unit.
target_link_libraries(atlantis_gltf_importer_tests
  PRIVATE
    atlantis_gltf_importer_lib
    # Test-only: the fallback-texture test parses the importer's generated
    # DDS with the cooker's own parser. The importer library itself never
    # links the cooker (ADR-0084).
    atlantis_asset_cooker_lib
    Cgltf::Cgltf
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

set(ATLANTIS_BISTRO_CONTENT_DIR "${CMAKE_SOURCE_DIR}/content/bistro" CACHE PATH
  "Fetched Bistro content (tools/content/fetch_bistro.ps1); [bistro] tests SKIP when absent")

target_compile_definitions(atlantis_gltf_importer_tests PRIVATE
  ATLANTIS_BISTRO_CONTENT_DIR="${ATLANTIS_BISTRO_CONTENT_DIR}"
  ATLANTIS_GLTF_IMPORTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/tools/gltf_importer"
  # The end-to-end [bistro] test runs the real cooker binary on a sample of
  # the import's cook_manifest.txt.
  ATLANTIS_ASSET_COOKER_EXECUTABLE="$<TARGET_FILE:atlantis_asset_cooker>"
  # Plan 0047 P7: the committed catalog source, for the content-gated Bistro import root.
  ATLANTIS_ASSET_CATALOG_SOURCE_PATH="${CMAKE_SOURCE_DIR}/assets/asset_catalog.txt"
  # Plan 0047 M3: the import-twice determinism test runs the real importer.
  ATLANTIS_GLTF_IMPORTER_EXECUTABLE="$<TARGET_FILE:atlantis_gltf_importer>"
)
add_dependencies(atlantis_gltf_importer_tests atlantis_asset_cooker atlantis_gltf_importer)

catch_discover_tests(atlantis_gltf_importer_tests
  TEST_SPEC "~[bistro]"
  DISCOVERY_MODE PRE_TEST
)
catch_discover_tests(atlantis_gltf_importer_tests
  TEST_SPEC "[bistro]"
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "content" SKIP_RETURN_CODE 4
)
