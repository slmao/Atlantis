#include <atlantis/asset_system/mesh_artifact.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>

namespace atlantis::asset_system {

namespace {

constexpr std::array<char, 8> kMagic = {'A', 'T', 'L', 'M', 'E', 'S', 'H', '\0'};

void appendU16LE(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFU));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFFU));
}

void appendU32LE(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

void appendU64LE(std::vector<std::byte>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

// Reinterprets the IEEE-754 bit pattern via std::bit_cast (a same-size,
// same-machine reinterpretation, so the result is numerically correct
// regardless of host endianness), then serializes that pattern with the
// identical shift/mask routine used for every other integer field --
// never a memcpy of the float itself.
void appendFloatLE(std::vector<std::byte>& out, float value) {
  appendU32LE(out, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint16_t readU16LE(const std::byte* bytes) {
  return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[0]) | (static_cast<unsigned>(bytes[1]) << 8));
}

[[nodiscard]] std::uint32_t readU32LE(const std::byte* bytes) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
  return value;
}

[[nodiscard]] std::uint64_t readU64LE(const std::byte* bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  return value;
}

[[nodiscard]] float readFloatLE(const std::byte* bytes) {
  return std::bit_cast<float>(readU32LE(bytes));
}

}  // namespace

std::vector<std::byte> encodeMeshArtifact(AssetId assetId, const ParsedMeshSource& source) {
  std::vector<std::byte> out;
  out.reserve(kMeshArtifactHeaderSizeBytes + source.vertices.size() * kMeshArtifactVertexStrideBytes +
              source.indices.size() * 2);

  for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
  appendU32LE(out, kMeshArtifactSchemaVersion);
  appendU32LE(out, kMeshArtifactVertexStrideBytes);
  appendU64LE(out, assetId);
  appendU32LE(out, static_cast<std::uint32_t>(source.vertices.size()));
  appendU32LE(out, static_cast<std::uint32_t>(source.indices.size()));

  const auto vertexBytesOffset = static_cast<std::uint32_t>(kMeshArtifactHeaderSizeBytes);
  const std::uint32_t indexBytesOffset =
      vertexBytesOffset + static_cast<std::uint32_t>(source.vertices.size()) * kMeshArtifactVertexStrideBytes;
  appendU32LE(out, vertexBytesOffset);
  appendU32LE(out, indexBytesOffset);

  for (const MeshSourceVertex& v : source.vertices) {
    appendFloatLE(out, v.positionX);
    appendFloatLE(out, v.positionY);
    appendFloatLE(out, v.positionZ);
    appendFloatLE(out, v.colorR);
    appendFloatLE(out, v.colorG);
    appendFloatLE(out, v.colorB);
    appendFloatLE(out, v.uvU);
    appendFloatLE(out, v.uvV);
    appendFloatLE(out, v.normalX);
    appendFloatLE(out, v.normalY);
    appendFloatLE(out, v.normalZ);
  }

  for (std::uint16_t index : source.indices) appendU16LE(out, index);

  return out;
}

atlantis::Result<DecodedMeshArtifact, ArtifactDecodeError> decodeMeshArtifact(const std::vector<std::byte>& bytes) {
  using ResultT = atlantis::Result<DecodedMeshArtifact, ArtifactDecodeError>;

  if (bytes.size() < kMeshArtifactHeaderSizeBytes) return ResultT::Err(ArtifactDecodeError::TooSmallForHeader);

  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (bytes[i] != static_cast<std::byte>(kMagic[i])) return ResultT::Err(ArtifactDecodeError::BadMagic);
  }

  const std::uint32_t schemaVersion = readU32LE(bytes.data() + 8);
  if (schemaVersion != kMeshArtifactSchemaVersion) return ResultT::Err(ArtifactDecodeError::UnknownSchemaVersion);

  const std::uint32_t vertexStrideBytes = readU32LE(bytes.data() + 12);
  if (vertexStrideBytes != kMeshArtifactVertexStrideBytes) {
    return ResultT::Err(ArtifactDecodeError::UnsupportedVertexStride);
  }

  const AssetId assetId = readU64LE(bytes.data() + 16);
  const std::uint32_t vertexCount = readU32LE(bytes.data() + 24);
  const std::uint32_t indexCount = readU32LE(bytes.data() + 28);
  const std::uint32_t vertexBytesOffset = readU32LE(bytes.data() + 32);
  const std::uint32_t indexBytesOffset = readU32LE(bytes.data() + 36);

  if (vertexCount == 0 || vertexCount > 65535) return ResultT::Err(ArtifactDecodeError::VertexCountOutOfRange);
  if (indexCount == 0 || indexCount % 3 != 0) return ResultT::Err(ArtifactDecodeError::IndexCountNotMultipleOfThree);

  // Every size computed in uint64_t before comparison/allocation, so a
  // header crafted to overflow a 32-bit product cannot drive an
  // oversized or wrapped-around allocation.
  const std::uint64_t expectedVertexBytesOffset = kMeshArtifactHeaderSizeBytes;
  const std::uint64_t expectedIndexBytesOffset =
      expectedVertexBytesOffset + static_cast<std::uint64_t>(vertexCount) * vertexStrideBytes;
  const std::uint64_t expectedTotalSize = expectedIndexBytesOffset + static_cast<std::uint64_t>(indexCount) * 2;

  if (vertexBytesOffset != expectedVertexBytesOffset || indexBytesOffset != expectedIndexBytesOffset) {
    return ResultT::Err(ArtifactDecodeError::InconsistentOffsets);
  }
  if (static_cast<std::uint64_t>(bytes.size()) != expectedTotalSize) {
    return ResultT::Err(ArtifactDecodeError::SizeMismatch);
  }

  DecodedMeshArtifact decoded;
  decoded.assetId = assetId;
  decoded.vertexStrideBytes = vertexStrideBytes;
  decoded.vertexBytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(vertexBytesOffset),
                              bytes.begin() + static_cast<std::ptrdiff_t>(indexBytesOffset));

  // Every vertex float must be finite -- read from the bytes just
  // copied into decoded.vertexBytes above (never re-read from a
  // different buffer), so this check and the returned data can never
  // disagree. Plan 0020 Section P7/Spec 0020 D3: the normal's own
  // length-squared check runs only after every one of this vertex's
  // own 11 floats (not merely the three normal ones) is already
  // confirmed finite -- independently re-derived here, never trusting
  // the cooker's own already-performed check.
  for (std::uint32_t v = 0; v < vertexCount; ++v) {
    const std::byte* vertexStart = decoded.vertexBytes.data() + static_cast<std::size_t>(v) * vertexStrideBytes;
    for (std::size_t floatIndex = 0; floatIndex < 11; ++floatIndex) {
      const float value = readFloatLE(vertexStart + floatIndex * 4);
      if (!std::isfinite(value)) return ResultT::Err(ArtifactDecodeError::NonFiniteFloat);
    }

    const float normalX = readFloatLE(vertexStart + kMeshArtifactNormalOffsetBytes);
    const float normalY = readFloatLE(vertexStart + kMeshArtifactNormalOffsetBytes + 4);
    const float normalZ = readFloatLE(vertexStart + kMeshArtifactNormalOffsetBytes + 8);
    const double lengthSquared = detail::computeNormalLengthSquared(normalX, normalY, normalZ);
    if (!detail::isNormalLengthSquaredInTolerance(lengthSquared)) {
      return ResultT::Err(ArtifactDecodeError::NonUnitNormal);
    }
  }

  decoded.indices.reserve(indexCount);
  for (std::uint32_t i = 0; i < indexCount; ++i) {
    const std::byte* indexStart = bytes.data() + indexBytesOffset + static_cast<std::size_t>(i) * 2;
    const std::uint16_t value = readU16LE(indexStart);
    if (value >= vertexCount) return ResultT::Err(ArtifactDecodeError::IndexOutOfRange);
    decoded.indices.push_back(value);
  }

  return ResultT::Ok(std::move(decoded));
}

}  // namespace atlantis::asset_system
