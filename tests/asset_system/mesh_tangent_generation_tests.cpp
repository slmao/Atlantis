#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <atlantis/asset_system/mesh_source.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] MeshSourceVertex makeVertex(float px, float py, float pz, float u, float v, float nx, float ny,
                                            float nz) {
  MeshSourceVertex vertex;
  vertex.positionX = px;
  vertex.positionY = py;
  vertex.positionZ = pz;
  vertex.colorR = 1.0f;
  vertex.colorG = 1.0f;
  vertex.colorB = 1.0f;
  vertex.uvU = u;
  vertex.uvV = v;
  vertex.normalX = nx;
  vertex.normalY = ny;
  vertex.normalZ = nz;
  return vertex;
}

[[nodiscard]] bool approxEqual(float a, float b, float epsilon = 1e-5f) { return std::abs(a - b) < epsilon; }

[[nodiscard]] bool tangentApproxEquals(const VertexTangent& t, float x, float y, float z, float w) {
  return approxEqual(t.x, x) && approxEqual(t.y, y) && approxEqual(t.z, z) && approxEqual(t.w, w);
}

}  // namespace

TEST_CASE("generateTangents computes a hand-computable flat-UV triangle's tangent exactly",
          "[asset_system][mesh_tangent_generation]") {
  // A single triangle in the XY plane, UVs matching position exactly
  // (u <- x, v <- y): e1 = (1,0,0), e2 = (0,1,0), d1 = (1,0), d2 =
  // (0,1), det = 1, T_face = e1 = (1,0,0), B_face = e2 = (0,1,0).
  // Every vertex shares normal (0,0,1), already orthogonal to T_face
  // and already unit length, so orthogonalization is a no-op.
  // h_face = sign(dot(cross((0,0,1),(1,0,0)), (0,1,0))) =
  // sign(dot((0,1,0),(0,1,0))) = sign(1) = +1.
  ParsedMeshSource source;
  source.vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
  };
  source.indices = {0, 1, 2};

  const auto result = generateTangents(source);
  REQUIRE(result.isOk());
  const auto& tangents = result.value();
  REQUIRE(tangents.size() == 3);
  for (const VertexTangent& t : tangents) CHECK(tangentApproxEquals(t, 1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_CASE("generateTangents falls back to the deterministic axis rule for a UV-degenerate triangle's own vertices",
          "[asset_system][mesh_tangent_generation]") {
  // ADR-0073 Decision item 4a: every vertex of this one triangle has
  // zero non-degenerate contribution (the triangle's own UV is
  // exactly degenerate -- all three vertices share the same UV, so
  // det == 0), so all three land on the fallback path. Normal (0,0,1)
  // is equidistant from X and Y (both dot to 0) and farthest from Z
  // (dot 1) -- P3's own fixed tie-break (X before Y before Z) picks
  // X: T_raw = X - N*dot(N,X) = (1,0,0), already unit length; tw is
  // fixed at +1.0 for a fallback vertex.
  ParsedMeshSource source;
  source.vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
  };
  source.indices = {0, 1, 2};

  const auto result = generateTangents(source);
  REQUIRE(result.isOk());
  const auto& tangents = result.value();
  REQUIRE(tangents.size() == 3);
  for (const VertexTangent& t : tangents) CHECK(tangentApproxEquals(t, 1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_CASE(
    "generateTangents excludes a geometrically-degenerate, UV-non-degenerate triangle from contributing, mirroring "
    "pbr_sphere's own real pole-closing-triangle case",
    "[asset_system][mesh_tangent_generation]") {
  // ADR-0073's own Accepted Correction, 2026-09-06: vertex 0 and
  // vertex 2 are authored at the identical 3D position (0,0,0) but
  // differ in UV -- exactly pbr_sphere's own real pole-duplicate
  // structure. e2 = pos(v2) - pos(v0) = (0,0,0), so area2 =
  // |cross(e1, e2)| = 0 exactly, while edgeScale is dominated by the
  // real, nonzero e1/e3 edges (never hits the edgeScale == 0 branch)
  // -- geometricRatio = 0, well below 1e-12. The triangle's own UV
  // determinant is real and nonzero (d1=(1,0), d2=(0,1), det=1),
  // confirming this triangle is excluded purely for geometric, not
  // UV, degeneracy. With this the mesh's only triangle, all three
  // vertices land on the fallback path (same axis-X tie-break as
  // above, since all three share normal (0,0,1)).
  ParsedMeshSource source;
  source.vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
  };
  source.indices = {0, 1, 2};

  const auto result = generateTangents(source);
  REQUIRE(result.isOk());
  const auto& tangents = result.value();
  REQUIRE(tangents.size() == 3);
  for (const VertexTangent& t : tangents) CHECK(tangentApproxEquals(t, 1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_CASE("generateTangents rejects a real handedness conflict between two valid, non-degenerate triangles",
          "[asset_system][mesh_tangent_generation]") {
  // Two triangles share vertex 0 (position (0,0,0), normal (0,0,1)).
  // Triangle (0,1,2) is Test 1's own exact flat-UV shape, giving
  // h_face = +1 at vertex 0. Triangle (0,3,4) reuses the identical 3D
  // positions as vertices 1/2 (a real, non-degenerate triangle, not a
  // duplicate point) but with a mirrored UV parameterization (U/V
  // swapped between the two non-shared corners) -- a genuine UV
  // chirality flip, giving h_face = -1 at the same vertex 0: d1=(0,1),
  // d2=(1,0), det=-1, T_face=(0,1,0), B_face=(1,0,0),
  // cross((0,0,1),(0,1,0))=(-1,0,0), dot((-1,0,0),(1,0,0))=-1 -> h=-1.
  // Both triangles are individually valid (real 3D area, real UV
  // area) -- this is a genuine chirality disagreement, not an
  // artifact of a degenerate triangle's own arbitrary tie-broken sign.
  ParsedMeshSource source;
  source.vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),  // 0: shared vertex
      makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),  // 1
      makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),  // 2
      makeVertex(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),  // 3: same position as 1, mirrored UV
      makeVertex(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),  // 4: same position as 2, mirrored UV
  };
  source.indices = {0, 1, 2, 0, 3, 4};

  const auto result = generateTangents(source);
  REQUIRE(result.isErr());
  CHECK(result.error() == CookError::TangentHandednessConflict);
}

namespace {

// ADR-0073 Amendment 2026-09-19: a degenerate-basis vertex gets item 4a's
// fallback -- unit length, orthogonal to its normal, w = +1.
void checkIsAxisFallback(const VertexTangent& t, const MeshSourceVertex& v) {
  const double len = std::sqrt(double(t.x) * t.x + double(t.y) * t.y + double(t.z) * t.z);
  CHECK(std::abs(len - 1.0) < 1e-6);
  CHECK(std::abs(double(t.x) * v.normalX + double(t.y) * v.normalY + double(t.z) * v.normalZ) < 1e-6);
  CHECK(t.w == 1.0f);
}

}  // namespace

TEST_CASE("generateTangents gives a single-face tangent parallel to its normal the axis fallback",
          "[asset_system][mesh_tangent_generation]") {
  // Previously ADR-0073 item 5's DegenerateTangentBasis trigger; since the
  // 2026-09-19 Amendment it takes item 4a's fallback. The triangle is
  // neither geometrically nor UV degenerate (edgeScale = sqrt(2),
  // geometricRatio = 0.5; UV det = 1) and contributes T_face = (1,0,0),
  // B_face = (0,0,1) to vertex 0 -- whose authored normal is (1,0,0),
  // exactly parallel to T_face, so Gram-Schmidt yields the exact zero
  // vector. Normal (1,0,0): Y and Z tie at |dot| 0, the fixed tie-break
  // (X before Y before Z) picks Y, so T = (0,1,0), w = +1.
  ParsedMeshSource source;
  source.vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),  // 0: normal parallel to T_face
      makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f),
      makeVertex(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
  };
  source.indices = {0, 1, 2};

  const auto result = generateTangents(source);
  REQUIRE(result.isOk());
  CHECK(tangentApproxEquals(result.value()[0], 0.0f, 1.0f, 0.0f, 1.0f));
  checkIsAxisFallback(result.value()[0], source.vertices[0]);
}

TEST_CASE("generateTangents gives the axis fallback when two faces' tangents sum parallel to the normal",
          "[asset_system][mesh_tangent_generation]") {
  // Vertex 0 (normal +Z) gets T_face = (1,0,1) from triangle (0,1,2) and
  // T_face = (-1,0,1) from triangle (0,3,4); both have handedness +1 (no
  // conflict) and sum to (0,0,2), parallel to the normal, so the
  // orthogonalized tangent is the exact zero vector. Fallback for normal
  // +Z: X and Y tie at |dot| 0, X wins -> T = (1,0,0), w = +1. The other
  // vertices keep ordinary tangents.
  std::vector<MeshSourceVertex> vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(-1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
  };
  const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 3, 4};

  TangentGenerationStats stats;
  const auto result = generateTangentsU32(vertices, indices, &stats);
  REQUIRE(result.isOk());
  CHECK(tangentApproxEquals(result.value()[0], 1.0f, 0.0f, 0.0f, 1.0f));
  checkIsAxisFallback(result.value()[0], vertices[0]);
  CHECK(stats.degenerateBasisFallbacks == 1);
  CHECK(stats.zeroContributionFallbacks == 0);
  CHECK(tangentApproxEquals(result.value()[1], 1.0f, 0.0f, 0.0f, 1.0f));

  const auto again = generateTangentsU32(vertices, indices);
  REQUIRE(again.isOk());
  for (std::size_t v = 0; v < vertices.size(); ++v) {
    CHECK(again.value()[v].x == result.value()[v].x);
    CHECK(again.value()[v].y == result.value()[v].y);
    CHECK(again.value()[v].z == result.value()[v].z);
    CHECK(again.value()[v].w == result.value()[v].w);
  }
}

TEST_CASE("generateTangents gives the axis fallback when two faces' tangents cancel exactly",
          "[asset_system][mesh_tangent_generation]") {
  // Vertex 0 (normal +Z) gets T_face = (1,0,0) from triangle (0,1,2) and
  // T_face = (-1,0,0) from triangle (0,3,4) -- point-mirrored geometry with
  // identical UVs, so B_face flips too and both faces keep handedness +1.
  // The tangent sum is exactly (0,0,0). Fallback for normal +Z picks X:
  // T = (1,0,0), w = +1.
  std::vector<MeshSourceVertex> vertices = {
      makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(-1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
      makeVertex(0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
  };
  const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 3, 4};

  TangentGenerationStats stats;
  const auto result = generateTangentsU32(vertices, indices, &stats);
  REQUIRE(result.isOk());
  CHECK(tangentApproxEquals(result.value()[0], 1.0f, 0.0f, 0.0f, 1.0f));
  checkIsAxisFallback(result.value()[0], vertices[0]);
  CHECK(stats.degenerateBasisFallbacks == 1);
  CHECK(tangentApproxEquals(result.value()[3], -1.0f, 0.0f, 0.0f, 1.0f));
}

namespace {

[[nodiscard]] std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  REQUIRE(file.is_open());
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

[[nodiscard]] ParsedMeshSource loadRealMesh(const std::string& fileName) {
  const std::string path = std::string(ATLANTIS_ASSETS_MESHES_DIR) + "/" + fileName;
  const std::string text = readFile(path);
  const auto parsed = parseMeshSource(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

}  // namespace

TEST_CASE("generateTangents succeeds against all 5 real, currently-committed mesh sources",
          "[asset_system][mesh_tangent_generation]") {
  // Plan 0029's own corrected Existing-mesh tangent-generatability
  // audit (ADR-0073's own Accepted Correction, 2026-09-06): every one
  // of the 5 real, committed mesh sources cooks cleanly under the
  // corrected geometric-degeneracy-aware algorithm -- zero
  // TangentHandednessConflict, zero DegenerateTangentBasis. No source
  // edit was needed for any of them, including pbr_sphere.
  const std::array<std::pair<const char*, std::size_t>, 5> meshes = {{
      {"ground_plane.mesh.txt", 4},
      {"textured_quad_left.mesh.txt", 4},
      {"textured_quad_right.mesh.txt", 4},
      {"minimal_cube.mesh.txt", 8},
      {"pbr_sphere.mesh.txt", 425},
  }};

  for (const auto& [fileName, expectedVertexCount] : meshes) {
    const ParsedMeshSource source = loadRealMesh(fileName);
    CHECK(source.vertices.size() == expectedVertexCount);
    const auto result = generateTangents(source);
    INFO("mesh: " << fileName);
    REQUIRE(result.isOk());
    CHECK(result.value().size() == expectedVertexCount);
  }
}

TEST_CASE("generateTangents places pbr_sphere's own two geometrically-orphaned seam vertices on the fallback path",
          "[asset_system][mesh_tangent_generation]") {
  // ADR-0073's own Accepted Correction, 2026-09-06: under the
  // corrected geometric-degeneracy check, pbr_sphere's own vertices 24
  // (north-pole seam-closure duplicate) and 400 (south-pole
  // seam-closure duplicate) are referenced only by pole-closing
  // triangles now excluded as geometrically degenerate -- both fall
  // back to ADR-0073 Decision item 4a's deterministic axis rule.
  // Vertex 24's own real, authored normal is (-0, 1, 0); vertex 400's
  // is (0, -1, 1.2246468e-16) -- both equidistant from X and Z (dot 0
  // and ~1.2e-16 respectively, X strictly smaller in the second case)
  // and farthest from Y, so both pick axis X: T_raw = X - N*dot(N,X) =
  // X exactly (dot(N,X) == 0 for both), already unit length; tw fixed
  // at +1.0. Independently computed (Python, matching
  // generateTangents()'s own double-then-cast-to-float contract), not
  // transcribed from memory.
  const ParsedMeshSource source = loadRealMesh("pbr_sphere.mesh.txt");
  REQUIRE(source.vertices.size() == 425);
  CHECK(source.vertices[24].normalX == 0.0f);
  CHECK(source.vertices[24].normalY == 1.0f);
  CHECK(source.vertices[400].normalX == 0.0f);
  CHECK(source.vertices[400].normalY == -1.0f);

  const auto result = generateTangents(source);
  REQUIRE(result.isOk());
  const auto& tangents = result.value();
  CHECK(tangentApproxEquals(tangents[24], 1.0f, 0.0f, 0.0f, 1.0f));
  CHECK(tangentApproxEquals(tangents[400], 1.0f, 0.0f, 0.0f, 1.0f));
}
