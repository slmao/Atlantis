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
  src/cook.cpp
  src/load.cpp
  src/scene_source.cpp
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

# Plan 0012 Section D4/D9: declares one static mesh asset for
# atlantis_asset_cooker to process at build time, mirroring
# atlantis_add_slang_shader_pair()'s own stamp/BYPRODUCTS pattern
# (src/shader_system/CMakeLists.txt, ADR-0029) so CMake's staleness
# check only has to ask "does the stamp exist and is it newer than
# DEPENDS." NAME must be unique across all declared assets; SOURCE is
# the authoring source's own path relative to the fixed asset root
# (${CMAKE_SOURCE_DIR}/assets). Must be defined before any
# add_subdirectory() that calls it -- only directories processed after
# this one can see it, matching the existing
# atlantis_add_slang_shader_pair() ordering convention the root
# CMakeLists.txt already documents.
function(atlantis_add_static_mesh_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  # Strips the fixed ".mesh.txt" authoring extension to build the output
  # base name -- matches cook_command.cpp's own stripAuthoringExtension()
  # convention exactly, so the cooker's own output-path computation and
  # this custom command's own OUTPUT/BYPRODUCTS declarations never
  # disagree about where the cooked files land.
  string(REGEX REPLACE "\\.mesh\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.amesh")
  set(metadata_path "${output_dir}/${base}.amesh.meta.txt")
  set(stamp "${output_dir}/${base}.stamp")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}"
    COMMAND atlantis_asset_cooker
      --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp}
    DEPENDS ${source_path} atlantis_asset_cooker
    COMMENT "Cooking static mesh asset: ${ARG_SOURCE}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")

  set_property(GLOBAL APPEND PROPERTY ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS "${ARG_SOURCE}")

  # Exposed to the caller (e.g. Step 6's fixture consumer) so it can
  # add_dependencies() on the artifact without hand-reconstructing this
  # naming convention.
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0012 Section D4: writes every declared asset's logical path
# (accumulated via the global property above) to a single file at CMake
# GENERATE time -- before any build-time custom command runs, so there
# is no build-time race regardless of build parallelism -- and declares
# one validation target that runs the cooker's --validate-set mode over
# it, detecting an Asset ID collision, a case-only-differing logical
# path, or an exact duplicate across exactly this declared set (never a
# repository-global guarantee, per ADR-0044). Call exactly once, from
# the root CMakeLists.txt, after every add_subdirectory() that could
# declare an asset.
function(atlantis_finalize_asset_validation)
  get_property(declared_paths GLOBAL PROPERTY ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS)
  set(list_path "${CMAKE_BINARY_DIR}/assets/declared_assets.txt")
  list(JOIN declared_paths "\n" declared_paths_text)
  file(GENERATE OUTPUT "${list_path}" CONTENT "${declared_paths_text}\n")

  add_custom_target(atlantis_asset_validation ALL
    COMMAND atlantis_asset_cooker --validate-set --asset-list=${list_path}
    DEPENDS atlantis_asset_cooker
    COMMENT "Validating declared asset set for collisions"
    VERBATIM
  )
endfunction()
