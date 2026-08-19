# Plan 0012 Step 1 / ADR-0043: Atlantis Asset System depends on
# Atlantis::Core only -- no RHI, Renderer, RenderGraph, Shader System,
# Vulkan Backend, Platform, or Tools dependency, verified by
# tests/asset_system/module_boundary_tests.cpp's own include scan.
add_library(atlantis_asset_system STATIC
  src/static_mesh_asset_data.cpp
  src/logical_path.cpp
  src/asset_id.cpp
  src/asset_set_validation.cpp
  src/mesh_source.cpp
  src/asset_metadata.cpp
  src/mesh_artifact.cpp
)
add_library(Atlantis::AssetSystem ALIAS atlantis_asset_system)

target_include_directories(atlantis_asset_system
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_asset_system
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)
