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
