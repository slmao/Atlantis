# Plan 0012 Step 1 / ADR-0043: Atlantis Asset System depends on
# Atlantis::Core only -- no RHI, Renderer, RenderGraph, Shader System,
# Vulkan Backend, Platform, or Tools dependency, verified by
# tests/asset_system/module_boundary_tests.cpp's own include scan.
add_library(atlantis_asset_system STATIC
  src/static_mesh_asset_data.cpp
  src/logical_path.cpp
  src/asset_id.cpp
  src/asset_guid.cpp
  src/entity_ref.cpp
  src/asset_catalog_source.cpp
  src/asset_catalog.cpp
  src/mesh_source.cpp
  src/asset_metadata.cpp
  src/mesh_artifact.cpp
  src/mesh_tangent_generation.cpp
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
  src/environment_metadata.cpp
  src/environment_artifact.cpp
  src/load_environment.cpp
  src/environment_processing.cpp
  src/cook_environment.cpp
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

# Plan 0019 Section P5: real, compile-time enum-exhaustiveness protection
# for material_artifact.cpp's own kindToField() no-default switch over
# MaterialKind requires MSVC's C4062 ("enumerator ... not handled"),
# which is off by default even at /W4 -- matching
# src/runtime/CMakeLists.txt's own already-established /w14062
# precedent (classifyPresentationError()/classifySubmitError()) and
# src/tools/asset_cooker/CMakeLists.txt's own identical pattern. Scoped
# to this target only -- cmake/CompilerWarnings.cmake (shared by every
# target in this repository) is deliberately not touched. The
# repository's existing, already-present /WX promotes C4062 to a hard
# build error once this target opts into seeing it.
target_compile_options(atlantis_asset_system PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)

# Plan 0047 P7 (ADR-0098 D1): the committed catalog source every cook and
# import resolves its GUID from. Each cook DEPENDS on it (ruling O4): editing
# it re-cooks every declared asset, so no sidecar keeps a stale GUID.
# A cache entry, not a directory variable: the functions below expand it in
# their callers' directories (assets/, tests/).
set(ATLANTIS_ASSET_CATALOG_SOURCE "${CMAKE_SOURCE_DIR}/assets/asset_catalog.txt" CACHE INTERNAL
  "The committed catalog source (Plan 0047 P6)")

# Plan 0047 P12/P13: records one build step for the catalog assembly -- its
# target (the assembly depends on it) and the catalog fragment it writes.
function(_atlantis_register_catalog_fragment target fragment)
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_TARGETS "${target}")
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_CATALOG_FRAGMENTS "${fragment}")
endfunction()

# Plan 0047 P15: sets <out_var> to the GUID the committed catalog source
# records for <ROOT>:<PATH>, read at configure time -- the catalog source is a
# configure dependency, so editing it re-runs CMake. A missing entry fails
# configuration.
function(atlantis_catalog_guid out_var)
  cmake_parse_arguments(ARG "" "ROOT;PATH" "" ${ARGN})
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ATLANTIS_ASSET_CATALOG_SOURCE}")
  file(STRINGS "${ATLANTIS_ASSET_CATALOG_SOURCE}" entries REGEX "^asset: ")
  foreach(entry IN LISTS entries)
    string(REGEX REPLACE "\r$" "" entry "${entry}")
    if(entry MATCHES "^asset: guid=([0-9a-f-]+) type=[a-z_]+ root=([a-z]+) path=(.+)$")
      if(CMAKE_MATCH_2 STREQUAL ARG_ROOT AND CMAKE_MATCH_3 STREQUAL ARG_PATH)
        set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
      endif()
    endif()
  endforeach()
  message(FATAL_ERROR "atlantis_catalog_guid(): ${ARG_ROOT}:${ARG_PATH} has no entry in "
    "${ATLANTIS_ASSET_CATALOG_SOURCE}")
endfunction()

# Plan 0047 P15: where this build assembles its catalog (the Runtime's
# ATLANTIS_RUNTIME_ASSET_CATALOG_PATH and every test's ATLANTIS_ASSET_CATALOG_PATH).
set(ATLANTIS_ASSET_CATALOG_PATH "${CMAKE_BINARY_DIR}/asset_catalog.txt" CACHE INTERNAL
  "The assembled catalog (Plan 0047 P13)")

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
    BYPRODUCTS "${artifact_path}" "${metadata_path}" "${artifact_path}.catalog.txt"
    COMMAND atlantis_asset_cooker
      --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp} --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
    DEPENDS ${source_path} "${ATLANTIS_ASSET_CATALOG_SOURCE}" atlantis_asset_cooker
    COMMENT "Cooking static mesh asset: ${ARG_SOURCE}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")

  _atlantis_register_catalog_fragment(${ARG_NAME}_asset "${artifact_path}.catalog.txt")
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_DECLARATIONS "mesh\tassets\t${ARG_SOURCE}")

  # Exposed to the caller (e.g. Step 6's fixture consumer) so it can
  # add_dependencies() on the artifact without hand-reconstructing this
  # naming convention.
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0015 Section D7, Plan 0047 M5: declares one scene asset -- mirrors
# atlantis_add_static_mesh_asset()'s own stamp/BYPRODUCTS pattern. The scene's
# dependencies are no longer declared here: the assembled catalog records them
# from the cooked content (ADR-0098 D2), and the Runtime resolves them through
# it. Exports ATLANTIS_<NAME>_GUID, the scene's catalog GUID, read from the
# committed catalog source at configure time (P15). Must be defined before any
# add_subdirectory() that calls it.
function(atlantis_add_scene_asset)
  set(oneValueArgs NAME SOURCE)
  cmake_parse_arguments(ARG "" "${oneValueArgs}" "" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  string(REGEX REPLACE "\\.scene\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.ascene")
  set(metadata_path "${output_dir}/${base}.ascene.meta.txt")
  set(stamp "${output_dir}/${base}.stamp")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}" "${artifact_path}.catalog.txt"
    COMMAND atlantis_asset_cooker
      --kind=scene --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp} --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
    DEPENDS ${source_path} "${ATLANTIS_ASSET_CATALOG_SOURCE}" atlantis_asset_cooker
    COMMENT "Cooking scene asset: ${ARG_SOURCE}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  _atlantis_register_catalog_fragment(${ARG_NAME}_asset "${artifact_path}.catalog.txt")
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_DECLARATIONS "scene\tassets\t${ARG_SOURCE}")

  atlantis_catalog_guid(scene_guid ROOT assets PATH ${ARG_SOURCE})
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_GUID "${scene_guid}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0046 Milestone 3 (ADR-0094 Decision 1, Plan 0046 P9): one build step
# for one imported glTF scene -- one custom command, one stamp. It runs
# atlantis_gltf_importer (with the authored OVERLAY merged, ADR-0094
# Decision 3) into <build>/<NAME>/import, then atlantis_asset_cooker
# --kind=cook-manifest over that import into <build>/<NAME>/cooked. Its only inputs are the glTF, the
# overlay and the two tools -- a changed texture or buffer file under
# CONTENT_ROOT does not re-trigger it (the content is SHA-256 pinned).
# Exports ATLANTIS_<NAME>_scene_{ARTIFACT,METADATA}_PATH, _TARGET and
# _IMPORT_GUID (the import root's catalog GUID). Plan 0047 P12: the cook-manifest mode also writes
# <import dir>/import.catalog.txt, the import's one catalog fragment.
function(atlantis_add_imported_scene)
  set(oneValueArgs NAME GLTF CONTENT_ROOT OVERLAY)
  cmake_parse_arguments(ARG "" "${oneValueArgs}" "" ${ARGN})

  set(work_dir "${CMAKE_BINARY_DIR}/${ARG_NAME}")
  set(import_dir "${work_dir}/import")
  set(cooked_dir "${work_dir}/cooked")
  set(artifact_path "${cooked_dir}/${ARG_NAME}/${ARG_NAME}.ascene")
  set(metadata_path "${cooked_dir}/${ARG_NAME}/${ARG_NAME}.ascene.meta.txt")
  set(stamp "${work_dir}/${ARG_NAME}.stamp")
  get_filename_component(content_parent "${ARG_CONTENT_ROOT}" DIRECTORY)

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}" "${import_dir}/import.catalog.txt"
    # The importer refuses a non-empty output directory; the cooked
    # directory is rewritten in place.
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${import_dir}"
    COMMAND atlantis_gltf_importer
      --input=${ARG_GLTF} --content-root=${ARG_CONTENT_ROOT} --overlay=${ARG_OVERLAY}
      --output-dir=${import_dir} --name=${ARG_NAME} --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
    COMMAND atlantis_asset_cooker
      --kind=cook-manifest --import-dir=${import_dir} --cooked-dir=${cooked_dir}
      --content-parent=${content_parent} --stamp=${stamp}
    DEPENDS "${ARG_GLTF}" "${ARG_OVERLAY}" "${ATLANTIS_ASSET_CATALOG_SOURCE}" atlantis_gltf_importer
            atlantis_asset_cooker
    COMMENT "Importing and cooking imported scene: ${ARG_NAME}"
    VERBATIM
  )
  add_custom_target(atlantis_${ARG_NAME}_scene ALL DEPENDS "${stamp}")
  _atlantis_register_catalog_fragment(atlantis_${ARG_NAME}_scene "${import_dir}/import.catalog.txt")
  # Plan 0047 P7/P10: the import root declares itself as
  # content:<content-root name>/<glTF file name>.
  get_filename_component(content_root_name "${ARG_CONTENT_ROOT}" NAME)
  get_filename_component(gltf_name "${ARG_GLTF}" NAME)
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_DECLARATIONS
    "gltf_import\tcontent\t${content_root_name}/${gltf_name}")

  set(ATLANTIS_${ARG_NAME}_scene_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_scene_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  # Plan 0047 P15: the import root's catalog GUID (the Runtime derives the
  # scene's GUID from it).
  atlantis_catalog_guid(import_guid ROOT content PATH ${content_root_name}/${gltf_name})
  set(ATLANTIS_${ARG_NAME}_scene_IMPORT_GUID "${import_guid}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_scene_TARGET "atlantis_${ARG_NAME}_scene" PARENT_SCOPE)
  # Plan 0046 M3: the import directory, for the content-gated load test.
  set(ATLANTIS_${ARG_NAME}_scene_IMPORT_DIR "${import_dir}" PARENT_SCOPE)
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

  # Spec 0038: COLOR_SPACE is the PNG path's own knob and is deliberately
  # OMITTED for .dds sources -- the DDS file's own DXGI format is the sole
  # color-space authority there (the cooker's DDS branch never reads this
  # flag), so a DDS declaration passes no COLOR_SPACE at all.
  set(color_space_args "")
  if(ARG_COLOR_SPACE)
    list(APPEND color_space_args "--color-space=${color_space_lower}")
  endif()

  set(artifact_path "${output_dir}/${ARG_NAME}.atex")
  set(metadata_path "${output_dir}/${ARG_NAME}.atex.meta.txt")
  set(stamp "${output_dir}/${ARG_NAME}.stamp")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}" "${artifact_path}.catalog.txt"
    COMMAND atlantis_asset_cooker
      --kind=texture --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp} ${color_space_args} --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
    DEPENDS ${source_path} "${ATLANTIS_ASSET_CATALOG_SOURCE}" atlantis_asset_cooker
    COMMENT "Cooking texture asset: ${ARG_NAME} (${ARG_SOURCE}, ${ARG_COLOR_SPACE})"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")

  # Unconditional registration, matching atlantis_add_static_mesh_asset()
  # exactly. A texture SOURCE declared twice (the original, corrected-away
  # design) cooks twice under one catalog GUID, which the catalog assembly
  # rejects as DuplicateGuid (Plan 0047 P13).
  _atlantis_register_catalog_fragment(${ARG_NAME}_asset "${artifact_path}.catalog.txt")
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_DECLARATIONS "texture\tassets\t${ARG_SOURCE}")

  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0025 Milestone 2: declares one offline-cooked HDR environment.
# NAME owns build-output naming; SOURCE remains the normalized logical-path
# identity, matching the corrected ADR-0044 contract.
function(atlantis_add_environment_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")
  set(artifact_path "${output_dir}/${ARG_NAME}.aenv")
  set(metadata_path "${output_dir}/${ARG_NAME}.aenv.meta.txt")
  set(stamp "${output_dir}/${ARG_NAME}.stamp")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}" "${artifact_path}.catalog.txt"
    COMMAND atlantis_asset_cooker
      --kind=environment --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp} --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
    DEPENDS ${source_path} "${ATLANTIS_ASSET_CATALOG_SOURCE}" atlantis_asset_cooker
    COMMENT "Cooking environment asset: ${ARG_NAME} (${ARG_SOURCE})"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  _atlantis_register_catalog_fragment(${ARG_NAME}_asset "${artifact_path}.catalog.txt")
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_DECLARATIONS "environment\tassets\t${ARG_SOURCE}")
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0018 Section P5, Plan 0047 M5: declares one material asset for
# atlantis_asset_cooker to process at build time. Its texture references are
# GUIDs in the material source and are recorded as catalog dependencies from
# the cooked content, so no TEXTURE/NORMAL_MAP argument exists (Plan 0047 P16).
function(atlantis_add_material_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  string(REGEX REPLACE "\\.material\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.amaterial")
  set(metadata_path "${output_dir}/${base}.amaterial.meta.txt")
  set(stamp "${output_dir}/${base}.stamp")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}" "${artifact_path}.catalog.txt"
    COMMAND atlantis_asset_cooker
      --kind=material --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp} --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
    DEPENDS ${source_path} "${ATLANTIS_ASSET_CATALOG_SOURCE}" atlantis_asset_cooker
    COMMENT "Cooking material asset: ${ARG_NAME} (${ARG_SOURCE})"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  _atlantis_register_catalog_fragment(${ARG_NAME}_asset "${artifact_path}.catalog.txt")
  set_property(GLOBAL APPEND PROPERTY ATLANTIS_ASSET_DECLARATIONS "material\tassets\t${ARG_SOURCE}")
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()

# Plan 0047 P13 (ADR-0098 D2), replacing atlantis_finalize_asset_validation():
# one target assembles every catalog fragment of this build into
# <build>/asset_catalog.txt and writes the integrated_showcase_demo closure,
# <build>/integrated_showcase_demo.catalog.txt (the Android packaging set). It
# depends on every asset target and on each imported scene. Declarations,
# fragments and targets are gathered at the end of the top-level directory,
# so tests/ fixtures declared after this call are included. Call exactly
# once, from the root CMakeLists.txt.
function(atlantis_finalize_asset_catalog)
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _atlantis_write_asset_declarations)
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _atlantis_add_asset_catalog_target)
endfunction()

function(_atlantis_add_asset_catalog_target)
  atlantis_catalog_guid(closure_scene ROOT assets PATH scenes/integrated_showcase_demo.scene.txt)
  get_property(fragments GLOBAL PROPERTY ATLANTIS_ASSET_CATALOG_FRAGMENTS)
  get_property(asset_targets GLOBAL PROPERTY ATLANTIS_ASSET_TARGETS)
  set(fragment_list "${CMAKE_BINARY_DIR}/assets/catalog_fragments.txt")
  list(JOIN fragments "\n" fragments_text)
  file(GENERATE OUTPUT "${fragment_list}" CONTENT "${fragments_text}\n")

  set(catalog "${CMAKE_BINARY_DIR}/asset_catalog.txt")
  set(closure "${CMAKE_BINARY_DIR}/integrated_showcase_demo.catalog.txt")
  add_custom_target(atlantis_asset_catalog ALL
    COMMAND atlantis_asset_cooker --kind=assemble-catalog --catalog-source=${ATLANTIS_ASSET_CATALOG_SOURCE}
      --declarations=${CMAKE_BINARY_DIR}/assets/declarations.txt --fragment-list=${fragment_list}
      --out=${catalog} --closure=${closure_scene}=${closure}
    BYPRODUCTS "${catalog}" "${closure}"
    DEPENDS atlantis_asset_cooker
    COMMENT "Assembling the asset catalog"
    VERBATIM
  )
  add_dependencies(atlantis_asset_catalog ${asset_targets})
endfunction()

# Plan 0047 P10: every declaration as one "type<TAB>root<TAB>path" line, in
# declaration order -- the catalog assembly's declaration check.
function(_atlantis_write_asset_declarations)
  get_property(declarations GLOBAL PROPERTY ATLANTIS_ASSET_DECLARATIONS)
  list(JOIN declarations "\n" declarations_text)
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/assets/declarations.txt" CONTENT "${declarations_text}\n")
endfunction()
