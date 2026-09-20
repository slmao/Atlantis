#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// Plan 0037 (D2): schema-5 (.amesh v5, uint32_t indices) sibling codec
// tests, alongside the existing v4 mesh-artifact tests. The v4 pair's own
// tests are the structural precedent; the load-bearing new ground here is
// the >65,535-vertex round trip (v5's whole reason to exist) and the
// cross-version rejections in both directions.

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] MeshSourceVertex makeVertex(std::uint32_t index) {
  // A unit-normal quad-corner-ish vertex with per-index UV variation so
  // generated tangents are non-degenerate.
  const float u = static_cast<float>(index % 7) * 0.25f;
  const float v = static_cast<float>(index % 5) * 0.25f;
  MeshSourceVertex vertex;
  vertex.positionX = u;
  vertex.positionY = v;
  vertex.positionZ = 0.5f;
  vertex.colorR = 1.0f;
  vertex.colorG = 1.0f;
  vertex.colorB = 1.0f;
  vertex.uvU = u;
  vertex.uvV = v;
  vertex.normalX = 0.0f;
  vertex.normalY = 0.0f;
  vertex.normalZ = 1.0f;
  return vertex;
}

[[nodiscard]] std::vector<MeshSourceVertex> makeVertices(std::uint32_t count) {
  std::vector<MeshSourceVertex> vertices;
  vertices.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) vertices.push_back(makeVertex(i));
  return vertices;
}

}  // namespace

TEST_CASE("encodeMeshArtifactU32/decodeMeshArtifactU32 round-trip a small mesh", "[asset_system]") {
  ParsedMeshSource source;
  source.vertices = makeVertices(12);
  for (std::uint16_t i = 2; i < 12; i += 3) {
    source.indices.push_back(0);
    source.indices.push_back(1);
    source.indices.push_back(i);
  }
  const auto tangents = generateTangents(source);
  REQUIRE(tangents.isOk());

  const auto encoded = encodeMeshArtifactU32(0x0102030405060708ULL, source, tangents.value());
  const auto decoded = decodeMeshArtifactU32(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().assetId == 0x0102030405060708ULL);
  CHECK(decoded.value().vertexStrideBytes == kMeshArtifactVertexStrideBytes);
  CHECK(decoded.value().indices.size() == source.indices.size());
  for (std::size_t i = 0; i < source.indices.size(); ++i) {
    CHECK(decoded.value().indices[i] == source.indices[i]);
  }
}

TEST_CASE("schema 5 round-trips a mesh with more than 65,535 vertices", "[asset_system]") {
  // v5's reason to exist (D2/Ruling 5): Bistro's three real >65,535-vertex
  // primitives, mirrored by a synthetic 70,000-vertex fan.
  constexpr std::uint32_t kVertexCount = 70000;
  ParsedMeshSource source;
  source.vertices = makeVertices(kVertexCount);
  // ParsedMeshSource.indices is u16 (the existing cooker path's own field);
  // one triangle suffices for the encode input here -- the real >65535
  // index-width proof is the patched-u32-index check below, which uses
  // values ParsedMeshSource itself cannot represent.
  source.indices = {0, 1, 2};
  const auto tangents = generateTangents(source);
  REQUIRE(tangents.isOk());
  const auto encoded = encodeMeshArtifactU32(1, source, tangents.value());

  // Patch the header's index values? No -- encode widened real u32 indices
  // is proven by decode: assert the total size formula uses 4 bytes/index.
  const std::size_t expectedSize =
      kMeshArtifactHeaderSizeBytes + source.vertices.size() * kMeshArtifactVertexStrideBytes +
      source.indices.size() * 4;
  REQUIRE(encoded.size() == expectedSize);

  const auto decoded = decodeMeshArtifactU32(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().vertexBytes.size() == kVertexCount * kMeshArtifactVertexStrideBytes);

  // A hand-set u32 index at the artifact level (value > 65535, < vertex
  // count) round-trips -- the thing v4 cannot represent at all.
  std::vector<std::byte> patched = encoded;
  std::byte* indexStart = patched.data() + patched.size() - 4;
  for (int b = 0; b < 4; ++b) {
    indexStart[b] = static_cast<std::byte>((kVertexCount - 1) >> (8 * b) & 0xFFU);
  }
  const auto patchedDecoded = decodeMeshArtifactU32(patched);
  REQUIRE(patchedDecoded.isOk());
  CHECK(patchedDecoded.value().indices.back() == kVertexCount - 1);
}

TEST_CASE("the v4 decoder rejects schema 5 and vice versa", "[asset_system]") {
  ParsedMeshSource source;
  source.vertices = makeVertices(6);
  source.indices = {0, 1, 2, 3, 4, 5};
  const auto tangents = generateTangents(source);
  REQUIRE(tangents.isOk());

  const auto v5 = encodeMeshArtifactU32(7, source, tangents.value());
  CHECK(decodeMeshArtifact(v5).isErr());

  const auto v4 = encodeMeshArtifact(7, source, tangents.value());
  CHECK(decodeMeshArtifactU32(v4).isErr());
}

TEST_CASE("decodeMeshArtifactU32 rejects an out-of-range index", "[asset_system]") {
  ParsedMeshSource source;
  source.vertices = makeVertices(6);
  source.indices = {0, 1, 5};
  const auto tangents = generateTangents(source);
  REQUIRE(tangents.isOk());
  auto encoded = encodeMeshArtifactU32(7, source, tangents.value());
  // Last index (offset: size-4) -> vertexCount (6): out of range.
  std::byte* indexStart = encoded.data() + encoded.size() - 4;
  indexStart[0] = static_cast<std::byte>(6);
  const auto decoded = decodeMeshArtifactU32(encoded);
  REQUIRE(decoded.isErr());
  CHECK(decoded.error() == ArtifactDecodeError::IndexOutOfRange);
}

TEST_CASE("generateTangentsU32 matches the u16 entry point on the same mesh", "[asset_system]") {
  ParsedMeshSource source;
  source.vertices = makeVertices(24);
  for (std::uint16_t i = 2; i < 24; i += 3) {
    source.indices.push_back(0);
    source.indices.push_back(1);
    source.indices.push_back(i);
  }
  const auto viaU16 = generateTangents(source);
  REQUIRE(viaU16.isOk());

  std::vector<std::uint32_t> indicesU32(source.indices.begin(), source.indices.end());
  const auto viaU32 = generateTangentsU32(source.vertices, indicesU32);
  REQUIRE(viaU32.isOk());

  REQUIRE(viaU16.value().size() == viaU32.value().size());
  for (std::size_t i = 0; i < viaU16.value().size(); ++i) {
    CHECK(viaU16.value()[i].x == viaU32.value()[i].x);
    CHECK(viaU16.value()[i].y == viaU32.value()[i].y);
    CHECK(viaU16.value()[i].z == viaU32.value()[i].z);
    CHECK(viaU16.value()[i].w == viaU32.value()[i].w);
  }
}
