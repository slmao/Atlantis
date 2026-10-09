# Plan 0057 (Spec 0057): Atlantis Gameplay SDK's tests -- no GPU. The
# boundary scan and the committed generated headers (M1).
add_executable(atlantis_gameplay_sdk_tests
  gameplay_sdk_boundary_tests.cpp
  generated_binding_tests.cpp
  # Plan 0057 M2: the reflective layer and the query batch, over the
  # InProcess fixture (gameplay_fixture.h).
  reflective_tests.cpp
  # Plan 0057 M3: the typed layer, compatibility and compile-time refusals.
  typed_tests.cpp
  compile_check_tests.cpp
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
  ATLANTIS_GAMEPLAY_DEMO_SOURCE_DIR="${CMAKE_SOURCE_DIR}/examples/gameplay_demo"
)

catch_discover_tests(atlantis_gameplay_sdk_tests DISCOVERY_MODE PRE_TEST)

# Plan 0057 M4 (Spec 0057 R8; J8, J12): the example against a separate
# `atlantis_runtime --listen` process. Windows-only test code (the harness is
# copied from tests/cli/cli_e2e_tests.cpp); needs the example, so only when
# examples are built.
if(WIN32 AND ATLANTIS_BUILD_EXAMPLES)
  add_executable(atlantis_gameplay_e2e_tests gameplay_e2e_tests.cpp)
  target_link_libraries(atlantis_gameplay_e2e_tests
    PRIVATE
      Catch2::Catch2WithMain
      atlantis_compiler_warnings
  )
  target_compile_definitions(atlantis_gameplay_e2e_tests PRIVATE
    ATLANTIS_RUNTIME_EXE="$<TARGET_FILE:atlantis_runtime>"
    ATLANTIS_GAMEPLAY_DEMO_EXE="$<TARGET_FILE:atlantis_gameplay_demo>"
  )
  add_dependencies(atlantis_gameplay_e2e_tests atlantis_runtime atlantis_gameplay_demo)
  # Plan 0058 M5 (Q6; J10): the C# demo's case is compiled in only when C# is
  # enabled (cmake/AtlantisDotnet.cmake), and SKIPs otherwise.
  if(ATLANTIS_CSHARP_ENABLED)
    target_compile_definitions(atlantis_gameplay_e2e_tests PRIVATE
      ATLANTIS_DOTNET_EXE="${ATLANTIS_DOTNET}"
      ATLANTIS_CSHARP_GAMEPLAY_DEMO_DLL="${ATLANTIS_CSHARP_GAMEPLAY_DEMO_DLL}"
    )
    add_dependencies(atlantis_gameplay_e2e_tests atlantis_csharp_gameplay_demo)
  endif()
  catch_discover_tests(atlantis_gameplay_e2e_tests DISCOVERY_MODE PRE_TEST)
endif()

# Plan 0057 M3 (J3): three compile-failure probes, each with a twin that must
# compile. Each is an EXCLUDE_FROM_ALL object library built by its own ctest
# case; the failing variant passes only if the compiler's error is reported
# in probes/gameplay_probe.cpp itself (not a configuration or dependency
# failure), and its twin -- the same file with only the offending line
# swapped -- must build. RUN_SERIAL: the probes share one build tree.
set(_atlantis_probe_error_regex
    "gameplay_probe\\.cpp(\\([0-9]+(,[0-9]+)?\\)|:[0-9]+:[0-9]+): (fatal )?error")
foreach(_probe WRONG_KIND_SET READ_ONLY_SET READ_ONLY_ADD)
  string(TOLOWER "${_probe}" _name)
  foreach(_twin 0 1)
    set(_target "atlantis_gameplay_probe_${_name}_${_twin}")
    add_library(${_target} OBJECT EXCLUDE_FROM_ALL probes/gameplay_probe.cpp)
    target_link_libraries(${_target} PRIVATE Atlantis::GameplaySdk atlantis_compiler_warnings)
    target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${_target} PRIVATE ATLANTIS_PROBE_${_probe} ATLANTIS_PROBE_TWIN=${_twin})
    if(_twin)
      set(_test "gameplay_sdk compile probe: ${_name} twin compiles")
    else()
      set(_test "gameplay_sdk compile probe: ${_name} fails to compile")
    endif()
    add_test(NAME "${_test}"
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target ${_target})
    set_tests_properties("${_test}" PROPERTIES RUN_SERIAL TRUE LABELS "compile_probe")
    if(NOT _twin)
      set_tests_properties("${_test}" PROPERTIES PASS_REGULAR_EXPRESSION "${_atlantis_probe_error_regex}")
    endif()
  endforeach()
endforeach()
