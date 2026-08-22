# Plan 0014 Step 1 / ADR-0048: Atlantis::World depends on Atlantis::Core
# and, narrowly, Atlantis::AssetSystem (for AssetId only) -- no RHI,
# Renderer, RenderGraph, Shader System, Vulkan Backend, Platform,
# Runtime, or Tools dependency, verified by
# tests/world/module_boundary_tests.cpp's own include scan.
add_library(atlantis_world STATIC
  src/world.cpp
)
add_library(Atlantis::World ALIAS atlantis_world)

target_include_directories(atlantis_world
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Atlantis::AssetSystem is PUBLIC, not PRIVATE: renderable.h names
# atlantis::asset_system::AssetId in a public field type, so any
# consumer of <atlantis/world/renderable.h> must transitively see that
# type (matching how Atlantis::ShaderSystemRhiIntegration already links
# Atlantis::RHI PUBLIC for the identical reason).
target_link_libraries(atlantis_world
  PUBLIC
    Atlantis::Core
    Atlantis::AssetSystem
  PRIVATE
    atlantis_compiler_warnings
)
