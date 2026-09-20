#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Spec 0039 / Plan 0039 Milestone 3: the two meshes the index-type
// verification needs, written as real .amesh artifacts plus their
// metadata sidecars so they reach the GPU through the same
// setUpMinimalCubeFixtureFromAsset() composition root every other
// artifact-backed fixture uses.
//
// Lives here, in the fixture library, because both the GPU test and the
// golden generator must produce byte-identical meshes -- a golden
// captured from a generator that drifted from its test would be
// meaningless. Neither writer needs a cooker: the glTF importer is
// production's only schema-5 producer (Plan 0037 D2), and these call
// the public Asset System encoders directly. No Asset System type
// appears in this header, so the fixture library's own PRIVATE
// dependency on Atlantis::AssetSystem stays private.
namespace atlantis::image_regression {

struct MeshArtifactPaths {
  std::string artifactPath;
  std::string metadataPath;
};

// Spec 0039 T2's equivalence pair. One authored cube -- the exact
// vertices, colours, normals and winding of assets/meshes/
// minimal_cube.mesh.txt, which is also what this fixture's own
// hand-authored path draws -- encoded twice, by encodeMeshArtifact()
// and encodeMeshArtifactU32(), from the *same* ParsedMeshSource and the
// *same* generated tangents. The vertex bytes are therefore identical by
// construction and only the index width and the version field differ,
// which is precisely the claim the two-frame comparison tests.
[[nodiscard]] MeshArtifactPaths writeEquivalenceCubeV4(const std::filesystem::path& directory);
[[nodiscard]] MeshArtifactPaths writeEquivalenceCubeV5(const std::filesystem::path& directory);

// Spec 0039 T3's large mesh (Joint Human Review ruling Q2): a flat,
// closed-form grid with more vertices than a 16-bit index can address,
// sized so that half its rows sit above the ceiling rather than the
// ~4 rows a minimal 258x258 grid would have given.
//
// 362 x 362 = 131,044 vertices, 361 x 361 quads = 130,321 quads =
// 260,642 triangles = 781,926 indices. Vertex index 65,536 -- the first
// a uint16 cannot represent -- lands in row 181 of 362, so the grid's
// bottom half is drawn entirely from indices above the ceiling.
//
// Geometry: the unit square z = 0, x and y in [-0.5, +0.5], which is
// inside the cube's own footprint and therefore framed by the fixture's
// baked camera with no fixture change (Plan 0039 P5). Row 0 is the top
// edge (y = +0.5), row 361 the bottom (y = -0.5). Normals are (0,0,1)
// and tangents (1,0,0,1): unit, orthogonal and +-1-handed, so the v5
// decoder's per-vertex battery passes by construction.
//
// Colour is the truncation detector (Plan 0039 P6): every vertex below
// kLargeIndexGridHighBandFirstVertex is red, every vertex at or above it
// is blue. A 16-bit truncation wraps index 65,536 to 0, collapsing every
// high-band triangle onto the grid's first vertex -- the blue lower half
// disappears from the frame entirely, which the test asserts directly
// rather than leaving to the golden alone.
inline constexpr std::uint32_t kLargeIndexGridVerticesPerSide = 362;
inline constexpr std::uint32_t kLargeIndexGridVertexCount =
    kLargeIndexGridVerticesPerSide * kLargeIndexGridVerticesPerSide;  // 131,044
inline constexpr std::uint32_t kLargeIndexGridIndexCount =
    (kLargeIndexGridVerticesPerSide - 1) * (kLargeIndexGridVerticesPerSide - 1) * 6;  // 781,926
inline constexpr std::uint32_t kLargeIndexGridHighBandFirstVertex = 65536;  // the first index a uint16 cannot hold

[[nodiscard]] MeshArtifactPaths writeLargeIndexGridV5(const std::filesystem::path& directory);

}  // namespace atlantis::image_regression
