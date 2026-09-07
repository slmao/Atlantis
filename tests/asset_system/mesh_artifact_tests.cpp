#include <atlantis/asset_system/mesh_artifact.h>

#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

// Plan 0020 Section P2: the trailing three floats are the same real,
// unit-length placeholder normal reused verbatim from Spec 0020 D5's
// own already-approved literal, 0.577350269 -- lengthSquared computed
// (independently, via PowerShell's [double] arithmetic, not this
// codebase's own detail:: functions) as ~0.999999964, well inside
// [0.9801, 1.0201]. Plan 0029: indices {0, 0, 0} name a fully
// degenerate "triangle" (all three corners the same vertex, so
// edgeScale == 0) -- generateTangents() contributes nothing from it,
// so this single vertex always lands on the deterministic fallback
// path (ADR-0073 Decision item 4a), independent of Plan 0029's own
// geometric-degeneracy correction.
[[nodiscard]] ParsedMeshSource makeOneVertexSource() {
  ParsedMeshSource source;
  source.vertices = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 0.577350269f, 0.577350269f, 0.577350269f}};
  source.indices = {0, 0, 0};
  return source;
}

// The one vertex's own real, computed fallback tangent (ADR-0073
// Decision item 4a): the normal is equidistant from all three fixed
// axes, so the X-before-Y-before-Z tie-break picks X;
// T_raw = X - N * dot(N, X), normalized; tw = +1.0 always for a
// fallback vertex. Computed independently (Python, float32 rounding
// applied at each stored component, matching generateTangents()'s own
// double-then-cast-to-float contract) -- not transcribed from memory.
[[nodiscard]] std::vector<VertexTangent> makeOneVertexFallbackTangent() {
  return {VertexTangent{0.8164966106414795f, -0.40824827551841736f, -0.40824827551841736f, 1.0f}};
}

}  // namespace

TEST_CASE("encodeMeshArtifact matches an independently-computed expected byte vector", "[asset_system]") {
  // Pins the little-endian contract: independently computed (.NET's own
  // BitConverter.GetBytes(), not transcribed from memory) for
  // asset_id=0x0102030405060708 and a single vertex (1.0, 2.0, 3.0, 4.0,
  // 5.0, 6.0, 7.0, 8.0, 0.577350269, 0.577350269, 0.577350269) with
  // indices {0, 0, 0}, plus the vertex's own real, computed fallback
  // tangent (above). Plan 0029 Section P1/ADR-0073: the per-vertex
  // layout is now 60 bytes (position xyz, color rgb, UV0 uv, normal
  // xyz, tangent xyzw), so index_bytes_offset moves from 40+44=84 to
  // 40+60=100 and total size grows from 90 to 106. Catches a
  // regression to the wrong header layout, field order, or offsets --
  // and would catch a regression to host-endian encoding on genuinely
  // big-endian hardware. Disclosed limitation: every one of this
  // project's actual target platforms (x86-64 Windows, ARM/AArch64
  // Android) is little-endian, so on the hardware this test actually
  // runs on, an accidental native-struct memcpy would currently
  // produce the identical bytes this test expects -- the real
  // guarantee against that specific regression is the source-level
  // discipline in mesh_artifact.cpp itself (appendU32LE/appendU64LE/
  // appendFloatLE, never a memcpy of a multi-byte value), verified by
  // code review, not something a byte-comparison test on
  // little-endian-only hardware can fully enforce by itself. This
  // expected vector is a compile-time constant, computed once by an
  // independent tool -- this test never calls encodeMeshArtifact() to
  // produce its own expected value.
  const std::vector<std::byte> expected = {
      // Magic "ATLMESH\0"
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x4D}, std::byte{0x45}, std::byte{0x53},
      std::byte{0x48}, std::byte{0x00},
      // schema_version = 4
      std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // vertex_stride_bytes = 60
      std::byte{0x3C}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // asset_id = 0x0102030405060708, little-endian
      std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
      std::byte{0x02}, std::byte{0x01},
      // vertex_count = 1
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // index_count = 3
      std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // vertex_bytes_offset = 40
      std::byte{0x28}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // index_bytes_offset = 40 + 1*60 = 100
      std::byte{0x64}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // Vertex 0: position (1.0, 2.0, 3.0)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x40},
      // Vertex 0: color (4.0, 5.0, 6.0)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
      std::byte{0xA0}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x40},
      // Vertex 0: UV0 (7.0, 8.0)
      std::byte{0x00}, std::byte{0x00}, std::byte{0xE0}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x41},
      // Vertex 0: normal (0.577350269, 0.577350269, 0.577350269)
      std::byte{0x3A}, std::byte{0xCD}, std::byte{0x13}, std::byte{0x3F}, std::byte{0x3A}, std::byte{0xCD},
      std::byte{0x13}, std::byte{0x3F}, std::byte{0x3A}, std::byte{0xCD}, std::byte{0x13}, std::byte{0x3F},
      // Vertex 0: tangent (0.8164966..., -0.4082482..., -0.4082482..., 1.0) -- the
      // real, computed fallback tangent (above)
      std::byte{0xEC}, std::byte{0x05}, std::byte{0x51}, std::byte{0x3F}, std::byte{0xEB}, std::byte{0x05},
      std::byte{0xD1}, std::byte{0xBE}, std::byte{0xEB}, std::byte{0x05}, std::byte{0xD1}, std::byte{0xBE},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},
      // Indices {0, 0, 0}, each a std::uint16_t
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
  };
  REQUIRE(expected.size() == 106);

  const std::vector<std::byte> actual =
      encodeMeshArtifact(0x0102030405060708ULL, makeOneVertexSource(), makeOneVertexFallbackTangent());
  CHECK(actual == expected);
}

TEST_CASE("encodeMeshArtifact then decodeMeshArtifact round-trips exactly", "[asset_system]") {
  const AssetId assetId = 0x78c473ee2218581dULL;
  const auto source = makeOneVertexSource();
  const auto encoded = encodeMeshArtifact(assetId, source, makeOneVertexFallbackTangent());

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
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[0] = std::byte{0x00};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeMeshArtifact rejects the old, pre-UV0 schema version", "[asset_system]") {
  // Plan 0017 Section D2/ADR-0058: schema version 1 (24-byte,
  // position+color-only stride) is rejected outright -- no migration
  // reader. Unchanged by Plan 0020/0029: still rejected under schema
  // version 4's own check.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[8] = std::byte{0x01};  // schema_version's low byte, offset 8
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeMeshArtifact rejects the old, pre-normal schema version", "[asset_system]") {
  // Plan 0020 Section P1/ADR-0063: schema version 2 (32-byte,
  // position+color+UV0-only stride, no normal) is also rejected
  // outright, exactly like version 1 already was.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[8] = std::byte{0x02};  // schema_version's low byte, offset 8
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeMeshArtifact rejects the old, pre-tangent schema version", "[asset_system]") {
  // Plan 0029 Section P1/ADR-0073: schema version 3 (44-byte,
  // position+color+UV0+normal-only stride, no tangent) is now also
  // rejected outright, exactly like versions 1/2 already were.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[8] = std::byte{0x03};  // schema_version's low byte, offset 8
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeMeshArtifact rejects an unknown schema version", "[asset_system]") {
  // This byte value must name a schema version still genuinely
  // unrecognized now that 4 is the real, accepted version -- 5 here,
  // not 4.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[8] = std::byte{0x05};  // schema_version's low byte, offset 8
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeMeshArtifact rejects the old, pre-UV0 vertex stride", "[asset_system]") {
  // Plan 0017 Section D2/ADR-0058: stride 24 (the pre-UV0 layout) is
  // rejected outright, exactly like any other unsupported stride.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[12] = std::byte{0x18};  // vertex_stride_bytes's low byte, offset 12: 60 -> 24
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnsupportedVertexStride);
}

TEST_CASE("decodeMeshArtifact rejects the old, pre-normal vertex stride", "[asset_system]") {
  // Plan 0020 Section P1/ADR-0063: stride 32 (the pre-normal layout)
  // is also rejected outright.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[12] = std::byte{0x20};  // vertex_stride_bytes's low byte, offset 12: 60 -> 32
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnsupportedVertexStride);
}

TEST_CASE("decodeMeshArtifact rejects the old, pre-tangent vertex stride", "[asset_system]") {
  // Plan 0029 Section P1/ADR-0073: stride 44 (the pre-tangent layout)
  // is now also rejected outright, exactly like any other unsupported
  // stride -- the direct analog, one attribute later, of the
  // pre-UV0/pre-normal-stride cases above.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[12] = std::byte{0x2C};  // vertex_stride_bytes's low byte, offset 12: 60 -> 44
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnsupportedVertexStride);
}

TEST_CASE("decodeMeshArtifact rejects an unsupported vertex stride", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[12] = std::byte{0x10};  // vertex_stride_bytes's low byte, offset 12
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::UnsupportedVertexStride);
}

TEST_CASE("decodeMeshArtifact rejects a truncated buffer (size mismatch)", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes.pop_back();
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::SizeMismatch);
}

TEST_CASE("decodeMeshArtifact rejects inconsistent header offsets", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[32] = std::byte{0x29};  // vertex_bytes_offset's low byte, offset 32: 40 -> 41
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::InconsistentOffsets);
}

TEST_CASE("decodeMeshArtifact rejects an out-of-range index", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  // Plan 0029: the three index bytes now start at offset 40 + 1*60 =
  // 100 (the new 60-byte stride), not the old 84; set the first index
  // (a std::uint16_t, offset 100-101) to 5 (>= vertexCount == 1).
  bytes[100] = std::byte{0x05};
  bytes[101] = std::byte{0x00};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::IndexOutOfRange);
}

TEST_CASE("decodeMeshArtifact rejects a non-finite position/color vertex float", "[asset_system]") {
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
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

TEST_CASE("decodeMeshArtifact rejects a non-finite UV0 float", "[asset_system]") {
  // The UV0 U component (uvU) occupies offset 40 + 24 = 64 within the
  // first vertex's own 60-byte span (position 0-11, color 12-23, UV0
  // 24-31, normal 32-43, tangent 44-59) -- unaffected by Plan 0029's
  // own widening, since tangent is appended after normal, not
  // inserted before UV0.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[64] = std::byte{0xFF};
  bytes[65] = std::byte{0xFF};
  bytes[66] = std::byte{0xFF};
  bytes[67] = std::byte{0xFF};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonFiniteFloat);
}

TEST_CASE("decodeMeshArtifact rejects a non-finite normal float", "[asset_system]") {
  // The normal X component (normalX) occupies offset 40 + 32 = 72
  // within the first vertex's own 60-byte span -- unaffected by Plan
  // 0029's own widening.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[72] = std::byte{0xFF};
  bytes[73] = std::byte{0xFF};
  bytes[74] = std::byte{0xFF};
  bytes[75] = std::byte{0xFF};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonFiniteFloat);
}

TEST_CASE("decodeMeshArtifact rejects a non-finite tangent float", "[asset_system]") {
  // Plan 0029 Section P2/ADR-0073 Decision item 6: the tangent X
  // component occupies offset 40 + 44 = 84 within the first vertex's
  // own 60-byte span.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  bytes[84] = std::byte{0xFF};
  bytes[85] = std::byte{0xFF};
  bytes[86] = std::byte{0xFF};
  bytes[87] = std::byte{0xFF};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonFiniteFloat);
}

TEST_CASE("decodeMeshArtifact rejects a finite normal whose own length-squared is out of tolerance",
          "[asset_system]") {
  // Plan 0020 V8a: a real, hand-corrupted artifact byte buffer whose
  // normal region decodes to a finite value outside [0.9801, 1.0201] --
  // independently reachable at decode time, never merely re-running the
  // parse-time NonUnitNormal case. Corrupts the normal region (offset
  // 72-83, this vertex's own real, previously-valid 0.577350269 values)
  // to (2.0, 0.0, 0.0), lengthSquared = 4.0, far outside tolerance.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  // normalX = 2.0f little-endian: 0x00 0x00 0x00 0x40
  bytes[72] = std::byte{0x00};
  bytes[73] = std::byte{0x00};
  bytes[74] = std::byte{0x00};
  bytes[75] = std::byte{0x40};
  // normalY = 0.0f little-endian: 0x00 0x00 0x00 0x00
  bytes[76] = std::byte{0x00};
  bytes[77] = std::byte{0x00};
  bytes[78] = std::byte{0x00};
  bytes[79] = std::byte{0x00};
  // normalZ = 0.0f little-endian: 0x00 0x00 0x00 0x00
  bytes[80] = std::byte{0x00};
  bytes[81] = std::byte{0x00};
  bytes[82] = std::byte{0x00};
  bytes[83] = std::byte{0x00};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonUnitNormal);
}

TEST_CASE("decodeMeshArtifact rejects a finite tangent whose own length-squared is out of tolerance",
          "[asset_system]") {
  // Plan 0029 Section P2/ADR-0073 Decision item 6: the decode-time
  // twin of NonUnitNormal above, for the tangent's own xyz region
  // (offset 84-95). Corrupts it to (2.0, 0.0, 0.0), lengthSquared =
  // 4.0, far outside tolerance -- checked before orthogonality/
  // handedness, so this is reached regardless of either.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  // tangentX = 2.0f little-endian: 0x00 0x00 0x00 0x40
  bytes[84] = std::byte{0x00};
  bytes[85] = std::byte{0x00};
  bytes[86] = std::byte{0x00};
  bytes[87] = std::byte{0x40};
  // tangentY = 0.0f little-endian: 0x00 0x00 0x00 0x00
  bytes[88] = std::byte{0x00};
  bytes[89] = std::byte{0x00};
  bytes[90] = std::byte{0x00};
  bytes[91] = std::byte{0x00};
  // tangentZ = 0.0f little-endian: 0x00 0x00 0x00 0x00
  bytes[92] = std::byte{0x00};
  bytes[93] = std::byte{0x00};
  bytes[94] = std::byte{0x00};
  bytes[95] = std::byte{0x00};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonUnitTangent);
}

TEST_CASE("decodeMeshArtifact rejects a unit-length tangent that is not orthogonal to the normal",
          "[asset_system]") {
  // Plan 0029 Section P2/ADR-0073 Decision item 6: a real,
  // hand-corrupted tangent equal to the vertex's own normal --
  // unit-length (lengthSquared ~0.99999996, in tolerance, so
  // NonUnitTangent does not fire first) but dot(N, T) ~0.99999996,
  // far above the 1e-3 orthogonality tolerance. Independently computed
  // (Python), not transcribed from memory.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  // tangentX = tangentY = tangentZ = 0.577350269f little-endian: 0x3A 0xCD 0x13 0x3F
  bytes[84] = std::byte{0x3A};
  bytes[85] = std::byte{0xCD};
  bytes[86] = std::byte{0x13};
  bytes[87] = std::byte{0x3F};
  bytes[88] = std::byte{0x3A};
  bytes[89] = std::byte{0xCD};
  bytes[90] = std::byte{0x13};
  bytes[91] = std::byte{0x3F};
  bytes[92] = std::byte{0x3A};
  bytes[93] = std::byte{0xCD};
  bytes[94] = std::byte{0x13};
  bytes[95] = std::byte{0x3F};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::NonOrthogonalTangent);
}

TEST_CASE("decodeMeshArtifact rejects a tangent whose own handedness is not exactly +-1.0", "[asset_system]") {
  // Plan 0029 Section P2/ADR-0073 Decision item 6: the tangent's own
  // xyz stays the real, valid, orthogonal fallback tangent (so neither
  // NonUnitTangent nor NonOrthogonalTangent fires first) -- only the w
  // component (offset 96-99) is corrupted to 0.5, a discrete-flag
  // equality check, never a tolerance-based one.
  auto bytes = encodeMeshArtifact(1, makeOneVertexSource(), makeOneVertexFallbackTangent());
  // tangentW = 0.5f little-endian: 0x00 0x00 0x00 0x3F
  bytes[96] = std::byte{0x00};
  bytes[97] = std::byte{0x00};
  bytes[98] = std::byte{0x00};
  bytes[99] = std::byte{0x3F};
  const auto result = decodeMeshArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == ArtifactDecodeError::InvalidTangentHandedness);
}
