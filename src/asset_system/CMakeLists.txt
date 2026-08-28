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
  src/scene_metadata.cpp
  src/scene_artifact.cpp
  src/cook_scene.cpp
  src/decode_scene.cpp
  src/texture_metadata.cpp
  src/texture_artifact.cpp
  src/cook_texture.cpp
  src/load_texture.cpp
  src/material_source.cpp
  src/material_artifact.cpp
  src/material_metadata.cpp
  src/cook_material.cpp
  src/load_material.cpp
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
  # Plan 0015 Section D7: exposes the exact string cookStaticMesh() was
  # already given as its own logicalPathInput -- no behavior change to
  # any existing caller; existing callers simply gain one more
  # unused-by-them exported variable. atlantis_add_scene_asset() below
  # is this variable's own first real consumer, recording a
  # MESH_DEPENDENCIES entry's logical path into the manifest.
  set(ATLANTIS_${ARG_NAME}_LOGICAL_PATH "${ARG_SOURCE}" PARENT_SCOPE)
endfunction()

# Plan 0015 Section D7: declares one scene asset -- mirrors
# atlantis_add_static_mesh_asset()'s own stamp/BYPRODUCTS pattern, with
# two additions: a MESH_DEPENDENCIES list (each entry must already be
# declared via atlantis_add_static_mesh_asset() above) and a generated,
# build-tree-private manifest recording each dependency's own logical
# path, artifact path, and metadata path as a tab-separated triple
# (Section D8) -- Runtime's own sole way to resolve a scene's mesh
# references to real on-disk artifacts, never a portable part of the
# artifact itself. Must be defined before any add_subdirectory() that
# calls it, matching atlantis_add_static_mesh_asset()'s own ordering
# convention exactly.
function(atlantis_add_scene_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE)
  # Plan 0018 Section P9: MATERIAL_DEPENDENCIES/TEXTURE_DEPENDENCIES
  # extend the same manifest MESH_DEPENDENCIES already populates -- still
  # one flat, unchanged, 3-column (logicalPath/artifactPath/metadataPath)
  # file, never a new column or a new file. TEXTURE_DEPENDENCIES exists
  # so a material's own transitively-referenced texture resolves through
  # the SAME manifest a scene's mesh references already use (ADR-0060 D5)
  # -- required even when the scene itself never directly names that
  # texture, an explicitly-permitted "declared but only transitively
  # used" entry, matching MESH_DEPENDENCIES' own existing tolerance for
  # an unreferenced declared dependency.
  set(multiValueArgs MESH_DEPENDENCIES MATERIAL_DEPENDENCIES TEXTURE_DEPENDENCIES)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  string(REGEX REPLACE "\\.scene\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.ascene")
  set(metadata_path "${output_dir}/${base}.ascene.meta.txt")
  set(manifest_path "${output_dir}/${base}.ascene.manifest.txt")
  set(stamp "${output_dir}/${base}.stamp")

  set(dependency_targets "")
  set(manifest_lines "")
  foreach(dep_name ${ARG_MESH_DEPENDENCIES})
    if(NOT DEFINED ATLANTIS_${dep_name}_ARTIFACT_PATH)
      message(FATAL_ERROR
        "atlantis_add_scene_asset(${ARG_NAME}): MESH_DEPENDENCIES entry "
        "'${dep_name}' is not a previously declared asset -- declare it "
        "via atlantis_add_static_mesh_asset() first.")
    endif()
    list(APPEND dependency_targets "${ATLANTIS_${dep_name}_TARGET}")
    string(APPEND manifest_lines
      "${ATLANTIS_${dep_name}_LOGICAL_PATH}\t${ATLANTIS_${dep_name}_ARTIFACT_PATH}\t${ATLANTIS_${dep_name}_METADATA_PATH}\n")
  endforeach()
  foreach(dep_name ${ARG_MATERIAL_DEPENDENCIES})
    if(NOT DEFINED ATLANTIS_${dep_name}_ARTIFACT_PATH)
      message(FATAL_ERROR
        "atlantis_add_scene_asset(${ARG_NAME}): MATERIAL_DEPENDENCIES entry "
        "'${dep_name}' is not a previously declared asset -- declare it "
        "via atlantis_add_material_asset() first.")
    endif()
    list(APPEND dependency_targets "${ATLANTIS_${dep_name}_TARGET}")
    string(APPEND manifest_lines
      "${ATLANTIS_${dep_name}_LOGICAL_PATH}\t${ATLANTIS_${dep_name}_ARTIFACT_PATH}\t${ATLANTIS_${dep_name}_METADATA_PATH}\n")
  endforeach()
  foreach(dep_name ${ARG_TEXTURE_DEPENDENCIES})
    if(NOT DEFINED ATLANTIS_${dep_name}_ARTIFACT_PATH)
      message(FATAL_ERROR
        "atlantis_add_scene_asset(${ARG_NAME}): TEXTURE_DEPENDENCIES entry "
        "'${dep_name}' is not a previously declared asset -- declare it "
        "via atlantis_add_texture_asset() first.")
    endif()
    list(APPEND dependency_targets "${ATLANTIS_${dep_name}_TARGET}")
    string(APPEND manifest_lines
      "${ATLANTIS_${dep_name}_LOGICAL_PATH}\t${ATLANTIS_${dep_name}_ARTIFACT_PATH}\t${ATLANTIS_${dep_name}_METADATA_PATH}\n")
  endforeach()

  # file(GENERATE) runs at CMake generate time, independent of which
  # config a multi-config (Visual Studio) generator later builds --
  # this manifest's own content (paths, not compiled binaries) does not
  # vary by config, matching atlantis_add_static_mesh_asset()'s own
  # ARTIFACT_PATH/METADATA_PATH (also config-independent, both already
  # proven working under this project's own real Visual Studio
  # generator). No $<CONFIG> generator expression is needed or used.
  file(GENERATE OUTPUT "${manifest_path}" CONTENT "${manifest_lines}")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}"
    COMMAND atlantis_asset_cooker
      --kind=scene --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp}
    DEPENDS ${source_path} atlantis_asset_cooker
    COMMENT "Cooking scene asset: ${ARG_SOURCE}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  # Ordering only, deliberately NOT added to the custom command's own
  # DEPENDS above: a MESH_DEPENDENCIES mesh's own content edit must not
  # re-trigger this scene's own cook step -- the scene artifact only
  # ever stores that mesh's AssetId (a hash of its logical path, known
  # already at CMake-declare time and unaffected by the mesh's content),
  # so the scene's own output bytes provably cannot change from a mesh
  # content edit. This add_dependencies() edge exists only so the full
  # build graph is consistent (the mesh builds as part of the same ALL
  # pass) -- it does not, by itself, mark this scene's own stamp stale.
  if(dependency_targets)
    add_dependencies(${ARG_NAME}_asset ${dependency_targets})
  endif()

  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_MANIFEST_PATH "${manifest_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0016 Section D8, corrected by the Plan's own "Human Review
# Correction -- 2026-08-24": declares one texture asset for
# atlantis_asset_cooker to process at build time, mirroring
# atlantis_add_static_mesh_asset()'s own stamp/BYPRODUCTS pattern.
# COLOR_SPACE is Unorm or Srgb, forwarded to the cooker's own
# --color-space= argument (lowercased here). Unlike
# atlantis_add_static_mesh_asset(), output paths are keyed by NAME, not
# SOURCE -- this is a build-output-file-naming convenience only (two
# custom-command outputs must never collide on one path), never an
# identity mechanism: every declared texture asset still needs its own
# distinct SOURCE, exactly like every other asset kind, so it gets its
# own distinct logical path and AssetId (ADR-0044). "Callable twice
# against the same SOURCE" is no longer this function's own supported
# shape -- see the Plan's own Correction for why that original design
# silently shared one AssetId across two genuinely different artifacts.
function(atlantis_add_texture_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE COLOR_SPACE)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  string(TOLOWER "${ARG_COLOR_SPACE}" color_space_lower)

  set(artifact_path "${output_dir}/${ARG_NAME}.atex")
  set(metadata_path "${output_dir}/${ARG_NAME}.atex.meta.txt")
  set(stamp "${output_dir}/${ARG_NAME}.stamp")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}"
    COMMAND atlantis_asset_cooker
      --kind=texture --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp} --color-space=${color_space_lower}
    DEPENDS ${source_path} atlantis_asset_cooker
    COMMENT "Cooking texture asset: ${ARG_NAME} (${ARG_SOURCE}, ${ARG_COLOR_SPACE})"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")

  # Unconditional registration, matching atlantis_add_static_mesh_asset()
  # exactly -- no collision-detector bypass. A texture SOURCE declared
  # twice (the original, corrected-away design) is now a genuine
  # DuplicateLogicalPath, caught by atlantis_finalize_asset_validation()'s
  # own --validate-set exactly like any other asset kind's duplicate.
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS "${ARG_SOURCE}")

  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
  # Plan 0018 Section P5 (Plan Review item 1): exposes the exact string
  # cookTexture() was already given as its own logicalPathInput --
  # additive only, no behavior change to any existing caller (every
  # existing caller simply gains one more unused-by-them exported
  # variable), matching atlantis_add_static_mesh_asset()'s own
  # ATLANTIS_<NAME>_LOGICAL_PATH precedent exactly.
  # atlantis_add_material_asset() below is this variable's own first
  # real consumer -- a scene's own TEXTURE_DEPENDENCIES entry needs it
  # to record a texture's manifest line, the same way
  # atlantis_add_scene_asset()'s existing MESH_DEPENDENCIES already uses
  # atlantis_add_static_mesh_asset()'s own _LOGICAL_PATH.
  set(ATLANTIS_${ARG_NAME}_LOGICAL_PATH "${ARG_SOURCE}" PARENT_SCOPE)
endfunction()

# Plan 0018 Section P5: declares one material asset for
# atlantis_asset_cooker to process at build time -- mirrors
# atlantis_add_scene_asset()'s own shape (a single named dependency
# instead of a list, since a Material references exactly one texture,
# ADR-0059's own closed DTO shape) more closely than
# atlantis_add_static_mesh_asset()'s own no-dependency shape. TEXTURE
# must already be declared via atlantis_add_texture_asset() above.
function(atlantis_add_material_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE TEXTURE)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  string(REGEX REPLACE "\\.material\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.amaterial")
  set(metadata_path "${output_dir}/${base}.amaterial.meta.txt")
  set(stamp "${output_dir}/${base}.stamp")

  if(NOT DEFINED ATLANTIS_${ARG_TEXTURE}_ARTIFACT_PATH)
    message(FATAL_ERROR
      "atlantis_add_material_asset(${ARG_NAME}): TEXTURE '${ARG_TEXTURE}' is not a "
      "previously declared texture asset -- declare it via atlantis_add_texture_asset() first.")
  endif()

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}"
    COMMAND atlantis_asset_cooker
      --kind=material --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp}
    DEPENDS ${source_path} atlantis_asset_cooker
    COMMENT "Cooking material asset: ${ARG_NAME} (${ARG_SOURCE})"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  # Ordering only, matching atlantis_add_scene_asset()'s own
  # MESH_DEPENDENCIES rationale exactly: the texture's own content does
  # not affect this material's own cooked bytes (only its AssetId,
  # resolved from the source file's own texture= logical-path string,
  # already known at configure time) -- this add_dependencies() exists
  # so the full build graph is consistent, not to mark this material's
  # stamp stale on a texture content edit.
  add_dependencies(${ARG_NAME}_asset ${ATLANTIS_${ARG_TEXTURE}_TARGET})

  set_property(GLOBAL APPEND PROPERTY ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS "${ARG_SOURCE}")
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_LOGICAL_PATH "${ARG_SOURCE}" PARENT_SCOPE)
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
