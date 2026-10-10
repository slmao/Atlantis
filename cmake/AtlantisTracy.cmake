# Tracy profiler client (wolfpld/tracy), the optional sink behind Atlantis's
# profiling API (ADR-0116, Spec 0059 ruling Q3). Included from the root
# CMakeLists.txt only when ATLANTIS_PROFILING and ATLANTIS_PROFILING_TRACY are
# both on and the target is not Android; a default build never reaches it.
include_guard(GLOBAL)

include(FetchContent)

# Pinned per ADR-0006/ADR-0116 D1 to the v0.14.1 release tag's archive, fetched
# by URL + URL_HASH (the cmake/AtlantisImGui.cmake shape). The hash was measured
# against this exact archive (6,173,646 bytes), downloaded twice from separate
# requests with identical results (Plan 0059 M2.1).
#
# SOURCE_SUBDIR names a directory with no CMakeLists.txt, so FetchContent only
# downloads and extracts: Tracy's own CMake project is not added, and only the
# client library below is built.
FetchContent_Declare(
  tracy
  URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
  URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR atlantis_no_cmake_project
)
FetchContent_MakeAvailable(tracy)

# The client library, compiled from its one amalgamated source file. SYSTEM keeps
# third-party warnings out of consumers' /W4 /WX builds; the library itself is
# built without this repository's warning flags. TRACY_ONLY_LOCALHOST: the
# listener binds the loopback interface only (the engine's rule for network
# surface, Spec 0055); TRACY_ON_DEMAND: no data is collected until a viewer
# connects. The definitions are PUBLIC so that the TUs that include Tracy's
# headers (Core's profile.cpp, one Vulkan Backend file) see TRACY_ENABLE too;
# both link this target PRIVATE, so no other module's compile line gets them.
add_library(atlantis_tracy STATIC ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
target_include_directories(atlantis_tracy SYSTEM PUBLIC ${tracy_SOURCE_DIR}/public)
target_compile_definitions(atlantis_tracy PUBLIC
  TRACY_ENABLE
  TRACY_ON_DEMAND
  TRACY_ONLY_LOCALHOST
  TRACY_NO_CALLSTACK
)
target_compile_definitions(atlantis_tracy PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(WIN32)
  target_link_libraries(atlantis_tracy PUBLIC ws2_32 dbghelp)
endif()
add_library(Tracy::Client ALIAS atlantis_tracy)
