# stb (nothings/stb), fetched once, unconditionally, independent of
# ATLANTIS_BUILD_TESTS. Included early from the root CMakeLists.txt --
# before add_subdirectory(src/tools/asset_cooker) -- so Stb::Stb exists
# as a target by the time the offline Asset Cooker needs it (Spec 0016,
# ADR-0041's own Accepted Amendment). Relocated verbatim from
# cmake/AtlantisDependencies.cmake, which previously declared this
# block only inside if(ATLANTIS_BUILD_TESTS) -- a real configure-order
# defect for a cooker target that must exist unconditionally. Same
# pinned commit hash, same URL_HASH, unchanged: this file does not
# re-pin stb.
include_guard(GLOBAL)

include(FetchContent)

# ADR-0041: stb (nothings/stb) has no git tags or GitHub Releases --
# pinned to a specific, full commit hash's archive instead of a tagged
# release, via URL + URL_HASH (not GIT_TAG): this environment's network
# resets git's smart-HTTP clone protocol consistently, while a plain
# HTTPS GET succeeds.
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
