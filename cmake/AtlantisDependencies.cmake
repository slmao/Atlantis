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
