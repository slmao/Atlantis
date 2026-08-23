# See Plan 0012 Section D2 for atlantis_asset_cooker's own dependency
# graph: it links Atlantis::AssetSystem and Atlantis::Core only -- no
# RHI, Renderer, or Shader System dependency.
#
# Mechanical split (matching src/tools/shader_compiler/CMakeLists.txt's
# own precedent): the argument-handling/orchestration logic lives in a
# small STATIC library, atlantis_asset_cooker_lib, rather than compiled
# directly into the atlantis_asset_cooker executable -- CMake cannot
# link a test executable against another executable's object files, and
# this logic must be directly unit-testable
# (tests/tools/asset_cooker/CMakeLists.txt links this same library).
add_library(atlantis_asset_cooker_lib STATIC
  cook_command.cpp
)

target_include_directories(atlantis_asset_cooker_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_asset_cooker_lib
  PUBLIC
    Atlantis::AssetSystem
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)

# Plan 0015 Section D7/D2 (V27): real, compile-time enum-exhaustiveness
# protection for sceneCookErrorMessage()'s own no-default switch over
# SceneCookError -- MSVC's C4062 ("enumerator ... not handled"), off by
# default even at /W4. Scoped to this target only, matching
# atlantis_runtime_host's own established /w14062 precedent
# (src/runtime/CMakeLists.txt) exactly -- cmake/CompilerWarnings.cmake
# (shared by every target) is deliberately not touched. The
# repository's existing, already-present /WX promotes C4062 to a hard
# build error once this target opts into seeing it.
target_compile_options(atlantis_asset_cooker_lib PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)

add_executable(atlantis_asset_cooker
  main.cpp
)

target_link_libraries(atlantis_asset_cooker
  PRIVATE
    atlantis_asset_cooker_lib
    atlantis_compiler_warnings
)
