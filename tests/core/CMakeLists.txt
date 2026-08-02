# Test framework acquisition per ADR-0006 (FetchContent, pinned) and
# ADR-0007 (Catch2 v3). Declared only here — must never become a
# dependency of atlantis_core or the demo executable.
#
# Implementation note (not a deviation from ADR-0006's decision): ADR-0006
# describes pinning via GIT_TAG. This declaration instead uses URL +
# URL_HASH — a SHA256-verified release tarball for the same v3.7.1 tag —
# because this environment's network resets git's smart-HTTP clone
# protocol consistently (verified: a plain HTTPS GET succeeds; `git
# clone`, including a shallow clone and after tuning
# http.postBuffer/http.version, does not). URL_HASH still satisfies every
# requirement ADR-0006 actually states: FetchContent-based, pinned to a
# specific tagged release, no package manager — and pins at least as
# strictly as GIT_TAG (an exact content hash vs. a mutable tag ref). This
# is a fetch-mechanism substitution forced by a network constraint, not a
# reconsidered dependency-management strategy; see ADR-0006 for the
# decision itself.
include(FetchContent)

FetchContent_Declare(
  Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
  URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2)

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

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
# PRE_TEST (not the default POST_BUILD): with a multi-config generator
# (Visual Studio), POST_BUILD discovery keeps only one shared test list,
# which the most recently built configuration silently overwrites —
# dropping ATLANTIS_ASSERT's Debug-only test from CTest's registration
# for every configuration once Release is built after Debug. PRE_TEST
# re-discovers per configuration at ctest-run time instead.
catch_discover_tests(atlantis_core_tests DISCOVERY_MODE PRE_TEST)
