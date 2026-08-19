# Plan 0012 Step 4b: the one static mesh asset this Plan ships --
# transcribed exactly from
# tests/image_regression/fixture/minimal_cube_fixture.cpp's own
# kCubeVertices/kCubeIndices arrays (see assets/meshes/minimal_cube.mesh.txt
# itself for the values).
atlantis_add_static_mesh_asset(
  NAME minimal_cube
  SOURCE meshes/minimal_cube.mesh.txt
)
