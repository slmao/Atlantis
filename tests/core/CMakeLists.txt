# Test framework (Catch2 v3, per ADR-0007) is fetched once by
# cmake/AtlantisDependencies.cmake, included from the root
# CMakeLists.txt — see that file for the FetchContent declaration and why
# URL+URL_HASH is used instead of ADR-0006's literal GIT_TAG wording, and
# plans/0002-platform-foundation.md Section 3 for why this declaration
# moved out of this file (it previously made tests/platform's ability to
# link Catch2::Catch2WithMain depend on this directory being processed
# first; neither test directory depends on the other now).

add_executable(atlantis_core_tests
  result_tests.cpp
  log_tests.cpp
  assert_tests.cpp
)

target_link_libraries(atlantis_core_tests
  PRIVATE
    Atlantis::Core
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST (not the default POST_BUILD): with a multi-config generator
# (Visual Studio), POST_BUILD discovery keeps only one shared test list,
# which the most recently built configuration silently overwrites —
# dropping ATLANTIS_ASSERT's Debug-only test from CTest's registration
# for every configuration once Release is built after Debug. PRE_TEST
# re-discovers per configuration at ctest-run time instead.
catch_discover_tests(atlantis_core_tests DISCOVERY_MODE PRE_TEST)
