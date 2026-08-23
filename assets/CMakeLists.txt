# Plan 0012 Step 4b: the one static mesh asset this Plan ships --
# transcribed exactly from
# tests/image_regression/fixture/minimal_cube_fixture.cpp's own
# kCubeVertices/kCubeIndices arrays (see assets/meshes/minimal_cube.mesh.txt
# itself for the values).
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
