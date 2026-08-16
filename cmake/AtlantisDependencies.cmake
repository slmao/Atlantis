# Shared third-party test/dev dependencies, fetched once. Per ADR-0006
# (FetchContent, pinned, no package manager) and ADR-0007 (Catch2 v3).
# Included once from the root CMakeLists.txt, before any test
# subdirectory that links Catch2::Catch2WithMain — see
# plans/0002-platform-foundation.md Section 3. This removes the prior
# tests/core -> tests/platform directory-ordering dependency: both now
# just link the target this file makes available, independently.
include_guard(GLOBAL)

include(FetchContent)

# See tests/core/CMakeLists.txt's original comment (preserved there) for
# why URL + URL_HASH is used instead of GIT_TAG: this environment's
# network resets git's smart-HTTP clone protocol consistently, while a
# plain HTTPS GET succeeds. Same ADR-0006 decision (FetchContent, pinned,
# no package manager), different acquisition mechanism.
FetchContent_Declare(
  Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
  URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# ADR-0041: stb (nothings/stb) has no git tags or GitHub Releases --
# pinned to a specific, full commit hash's archive instead of a tagged
# release, via the same URL/URL_HASH mechanism (not GIT_TAG) this file
# already uses for Catch2, for the same network-reliability reason
# documented above.
FetchContent_Declare(
  stb
  URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
  URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(stb)

# stb ships no CMakeLists.txt of its own (it is a pair of single-header
# libraries, not a CMake project) -- wrap its fetched source directory in
# a plain INTERFACE target, matching this repository's own target-naming
# convention (Atlantis::* / <Vendor>::<Lib> alias style).
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
add_library(Stb::Stb ALIAS stb)
