# Plan 0012 Step 4b: the one static mesh asset this Plan ships --
# transcribed exactly from
# tests/image_regression/fixture/minimal_cube_fixture.cpp's own
# kCubeVertices/kCubeIndices arrays (see assets/meshes/minimal_cube.mesh.txt
# itself for the values).
#
# Plan 0017 Section D6/ADR-0058: migrated to the version-2 grammar --
# every vertex line gained a trailing, explicit "0.0 0.0" UV0 pair.
# This is a deterministic placeholder only, never a per-face texture
# unwrap: this cube's own 8-vertex, shared-corner topology (each corner
# shared by the 3 faces meeting there) cannot express a correct
# per-face UV unwrap under any UV values, since a shared vertex can
# carry exactly one UV value, not one per face. minimal_cube continues
# to be drawn exclusively by minimal_mesh.slang, which declares no UV
# input and never reads these values -- this asset is not, and is not
# claimed to be, texture-ready.
atlantis_add_static_mesh_asset(
  NAME minimal_cube
  SOURCE meshes/minimal_cube.mesh.txt
)

# Mechanical requirement (no architectural effect), matching
# shaders/minimal_renderer/CMakeLists.txt's own identical, already-
# documented fix: the PARENT_SCOPE set() inside
# atlantis_add_static_mesh_asset() itself
# (src/asset_system/CMakeLists.txt) only reaches this directory's own
# scope -- CMake's PARENT_SCOPE always targets the immediate calling
# scope, never bubbles further up on its own. Step 6's consumer
# (tests/image_regression/CMakeLists.txt) is a sibling directory, a
# child of the root CMakeLists.txt, not of this directory, so it needs
# this second, explicit propagation up to root's own scope.
set(ATLANTIS_minimal_cube_ARTIFACT_PATH "${ATLANTIS_minimal_cube_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_minimal_cube_METADATA_PATH "${ATLANTIS_minimal_cube_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_minimal_cube_TARGET "${ATLANTIS_minimal_cube_TARGET}" PARENT_SCOPE)
# Plan 0015 Section D7: same second-propagation requirement as the
# three lines above, extended to the newly-exported LOGICAL_PATH
# variable -- a MESH_DEPENDENCIES consumer of atlantis_add_scene_asset()
# (Step 5's own test-only fixture in tests/asset_system/CMakeLists.txt,
# and Step 9's own real world_scene declaration, both sibling
# directories of this one) needs it visible at root scope to record
# minimal_cube's own logical path into a scene's generated manifest.
set(ATLANTIS_minimal_cube_LOGICAL_PATH "${ATLANTIS_minimal_cube_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0015 Section D11: the one scene asset this Plan ships --
# world_scene.scene.txt reproduces buildValidationScene()'s own former,
# now-removed six-entity validation scene byte-for-byte (every
# position/rotation/camera value transcribed directly from that
# function's own former source, not re-derived -- see Plan 0015
# Section D11). Depends on minimal_cube, already declared above.
atlantis_add_scene_asset(
  NAME world_scene
  SOURCE scenes/world_scene.scene.txt
  MESH_DEPENDENCIES minimal_cube
)
set(ATLANTIS_world_scene_ARTIFACT_PATH "${ATLANTIS_world_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_METADATA_PATH "${ATLANTIS_world_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_MANIFEST_PATH "${ATLANTIS_world_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_TARGET "${ATLANTIS_world_scene_TARGET}" PARENT_SCOPE)

# Plan 0016 Section D8/D11, corrected by the Plan's own "Human Review
# Correction -- 2026-08-24": the textured fixture's own two textures are
# two logically independent, byte-identical source PNGs (proven by
# tests/asset_system/texture_fixture_source_identity_tests.cpp), each
# with its own distinct SOURCE/NAME/COLOR_SPACE -- not one PNG cooked
# twice under a shared SOURCE. Two distinct SOURCE values give the two
# cooked artifacts two distinct logical paths and therefore two distinct
# AssetIds, restoring ADR-0044's one-asset-one-AssetId contract; the
# collision-detector bypass this used to require (a shared SOURCE
# registered only once) no longer applies and has been removed from
# atlantis_add_texture_asset() below. Declared here, unconditionally
# (like minimal_cube/world_scene above), rather than inside the
# ATLANTIS_BUILD_TESTS block tests/image_regression/fixture/ itself
# lives in -- this call site must precede
# atlantis_finalize_asset_validation()'s own call (below, this file's
# caller), which reads the accumulated declared-asset-paths property at
# CMake configure time, not build time; a declaration added only inside
# the later ATLANTIS_BUILD_TESTS block would never be seen by that read.
atlantis_add_texture_asset(
  NAME textured_quad_unorm
  SOURCE textures/textured_quad_source_unorm.png
  COLOR_SPACE Unorm
)
set(ATLANTIS_textured_quad_unorm_ARTIFACT_PATH "${ATLANTIS_textured_quad_unorm_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_unorm_METADATA_PATH "${ATLANTIS_textured_quad_unorm_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_unorm_TARGET "${ATLANTIS_textured_quad_unorm_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME textured_quad_srgb
  SOURCE textures/textured_quad_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_textured_quad_srgb_ARTIFACT_PATH "${ATLANTIS_textured_quad_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_srgb_METADATA_PATH "${ATLANTIS_textured_quad_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_srgb_TARGET "${ATLANTIS_textured_quad_srgb_TARGET}" PARENT_SCOPE)
