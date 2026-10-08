# Plan 0057 M1 (Spec 0057 R7; ADR-0111 D1; J2, J4): the binding generator's
# tests and the staleness of the two committed headers it produced.
add_executable(atlantis_sdk_codegen_tests
  sdk_codegen_tests.cpp
)

target_link_libraries(atlantis_sdk_codegen_tests
  PRIVATE
    atlantis_sdk_codegen_lib
    # worldSchema(), and connection::text's leavesOf() as the leaf-order
    # cross-check (Plan 0057 J5's shared grammar).
    Atlantis::Connection
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_sdk_codegen_tests PRIVATE
  ATLANTIS_SDK_GENERATED_WORLD_H="${CMAKE_SOURCE_DIR}/src/gameplay_sdk/include/atlantis/gameplay/generated/world.h"
  ATLANTIS_SDK_GENERATED_SYNTHETIC_H="${CMAKE_SOURCE_DIR}/tests/gameplay_sdk/generated/synthetic.h"
  ATLANTIS_SDK_CODEGEN_EXPECTED_DIR="${CMAKE_BINARY_DIR}/sdk_codegen"
)

catch_discover_tests(atlantis_sdk_codegen_tests DISCOVERY_MODE PRE_TEST)
