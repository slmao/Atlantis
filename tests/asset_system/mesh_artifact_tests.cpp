#include <atlantis/asset_system/mesh_artifact.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] ParsedMeshSource makeOneVertexSource() {
  ParsedMeshSource source;
  source.vertices = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}};
  source.indices = {0, 0, 0};
  return source;
}

}  // namespace

TEST_CASE("encodeMeshArtifact matches an independently-computed expected byte vector", "[asset_system]") {
  // Pins the little-endian contract: independently computed (Python's
  // struct.pack('<...', ...), not transcribed from memory) for
  // asset_id=0x0102030405060708 and a single vertex
  // (1.0, 2.0, 3.0, 4.0, 5.0, 6.0) with indices {0, 0, 0}. A
  // determinism-only test would pass even if the writer emitted
  // host-endian bytes; this test would not.
  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x4D}, std::byte{0x45}, std::byte{0x53},
      std::byte{0x48}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x18}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x08}, std::byte{0x07},
      std::byte{0x06}, std::byte{0x05}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x03}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x28}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x80}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x40},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
  };
  REQUIRE(expected.size() == 70);

  const std::vector<std::byte> actual = encodeMeshArtifact(0x0102030405060708ULL, makeOneVertexSource());
  CHECK(actual == expected);
}

TEST_CASE("encodeMeshArtifact then decodeMeshArtifact round-trips exactly", "[asset_system]") {
  const AssetId assetId = 0x78c473ee2218581dULL;
  const auto source = makeOneVertexSource();
  const auto encoded = encodeMeshArtifact(assetId, source);

  const auto decoded = decodeMeshArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().assetId == assetId);
  CHECK(decoded.value().vertexStrideBytes == kMeshArtifactVertexStrideBytes);
  CHECK(decoded.value().vertexBytes.size() == source.vertices.size() * kMeshArtifactVertexStrideBytes);
  CHECK(decoded.value().indices == source.indices);
}

TEST_CASE("decodeMeshArtifact rejects a buffer too small for the header", "[asset_system]") {
  const std::vector<std::byte> tooSmall(10, std::byte{0});
  const auto result = decodeMeshArtifact(tooSmall);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::TooSmallForHeader);
}

TEST_CASE("decodeMeshArtifact rejects a bad magic", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  bytes[0] = std::byte{0x00};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeMeshArtifact rejects an unknown schema version", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  bytes[8] = std::byte{0x02};  // schema_version's low byte, offset 8
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeMeshArtifact rejects an unsupported vertex stride", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  bytes[12] = std::byte{0x10};  // vertex_stride_bytes's low byte, offset 12
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnsupportedVertexStride);
}

TEST_CASE("decodeMeshArtifact rejects a truncated buffer (size mismatch)", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  bytes.pop_back();
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::SizeMismatch);
}

TEST_CASE("decodeMeshArtifact rejects inconsistent header offsets", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  bytes[32] = std::byte{0x29};  // vertex_bytes_offset's low byte, offset 32: 40 -> 41
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::InconsistentOffsets);
}

TEST_CASE("decodeMeshArtifact rejects an out-of-range index", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  // The three index bytes start at offset 40 + 1*24 = 64; set the first
  // index (a std::uint16_t, offset 64-65) to 5 (>= vertexCount == 1).
  bytes[64] = std::byte{0x05};
  bytes[65] = std::byte{0x00};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::IndexOutOfRange);
}

TEST_CASE("decodeMeshArtifact rejects a non-finite vertex float", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource());
  // The first vertex float (positionX) occupies offset 40-43. Setting
  // every byte to 0xFF produces a quiet-NaN bit pattern.
  bytes[40] = std::byte{0xFF};
  bytes[41] = std::byte{0xFF};
  bytes[42] = std::byte{0xFF};
  bytes[43] = std::byte{0xFF};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonFiniteFloat);
}
