# Plan 0065 M2: Atlantis Authoring -- the session and its InProcess
# AuthoringConnection, validity, the save round trip and the module boundary.
# GPU-free. The J9 measurement ([.j9]) is hidden: run by hand on Release.
add_executable(atlantis_authoring_tests
  authoring_session_tests.cpp
  authoring_validity_tests.cpp
  authoring_round_trip_tests.cpp
  authoring_boundary_tests.cpp
  authoring_measurement_tests.cpp
)

target_link_libraries(atlantis_authoring_tests
  PRIVATE
    Atlantis::Authoring
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_authoring_tests PRIVATE
  ATLANTIS_AUTHORING_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/authoring"
  ATLANTIS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

# The imported Bistro scene, when its content-gated build step exists (the
# Plan 0046 M3 pattern), for the J9 measurement.
if(DEFINED ATLANTIS_bistro_scene_TARGET)
  target_compile_definitions(atlantis_authoring_tests PRIVATE
    ATLANTIS_BISTRO_SCENE_IMPORT_DIR="${ATLANTIS_bistro_scene_IMPORT_DIR}"
    ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH="${ATLANTIS_bistro_scene_ARTIFACT_PATH}"
  )
endif()

catch_discover_tests(atlantis_authoring_tests DISCOVERY_MODE PRE_TEST)

# Plan 0065 M3 (P7; Spec 0065 R12, Q7 T-a; ADR-0130 D4): the second edit target,
# test-only -- an EditTarget over AuthoringConnection running E2's EditHistory
# unchanged. Its own executable, since it also links Atlantis Editor's model;
# Atlantis Authoring itself does not.
add_executable(atlantis_authoring_edit_target_tests
  authoring_edit_target_tests.cpp
)

target_link_libraries(atlantis_authoring_edit_target_tests
  PRIVATE
    Atlantis::Authoring
    Atlantis::Editor
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_authoring_edit_target_tests DISCOVERY_MODE PRE_TEST)
