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
