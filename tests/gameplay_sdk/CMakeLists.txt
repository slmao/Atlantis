# Plan 0057 (Spec 0057): Atlantis Gameplay SDK's tests -- no GPU. The
# boundary scan and the committed generated headers (M1).
add_executable(atlantis_gameplay_sdk_tests
  gameplay_sdk_boundary_tests.cpp
  generated_binding_tests.cpp
)

target_link_libraries(atlantis_gameplay_sdk_tests
  PRIVATE
    Atlantis::GameplaySdk
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# generated/synthetic.h resolves next to the test sources; the synthetic
# descriptor table it was generated from lives with the generator's tests.
target_include_directories(atlantis_gameplay_sdk_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/tools/sdk_codegen
)

target_compile_definitions(atlantis_gameplay_sdk_tests PRIVATE
  ATLANTIS_GAMEPLAY_SDK_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/gameplay_sdk"
)

catch_discover_tests(atlantis_gameplay_sdk_tests DISCOVERY_MODE PRE_TEST)
