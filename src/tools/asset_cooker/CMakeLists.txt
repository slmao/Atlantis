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

add_executable(atlantis_asset_cooker
  main.cpp
)

target_link_libraries(atlantis_asset_cooker
  PRIVATE
    atlantis_asset_cooker_lib
    atlantis_compiler_warnings
)
