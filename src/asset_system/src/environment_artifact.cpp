#include <atlantis/asset_system/environment_artifact.h>

#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace atlantis::asset_system {

namespace {

constexpr std::array<char, 8> kMagic = {'A', 'T', 'L', 'E', 'N', 'V', '\0', '\0'};

void appendU16LE(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFU));
  out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void appendU32LE(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
}

void appendU64LE(std::vector<std::byte>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
}

void appendFloatLE(std::vector<std::byte>& out, float value) { appendU32LE(out, std::bit_cast<std::uint32_t>(value)); }

[[nodiscard]] std::uint16_t readU16LE(const std::byte* bytes) {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) |
                                    (static_cast<std::uint16_t>(bytes[1]) << 8U));
}

[[nodiscard]] std::uint32_t readU32LE(const std::byte* bytes) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8U * i);
  return value;
}

[[nodiscard]] std::uint64_t readU64LE(const std::byte* bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (8U * i);
  return value;
}

[[nodiscard]] bool isPowerOfTwo(std::uint32_t value) { return value != 0 && (value & (value - 1U)) == 0; }

[[nodiscard]] std::uint32_t fullMipCount(std::uint32_t dimension) {
  std::uint32_t count = 0;
  while (dimension != 0) {
    ++count;
    dimension >>= 1U;
  }
  return count;
}

[[nodiscard]] bool checkedExpectedSizes(std::uint32_t faceSize, std::uint32_t mipCount, std::uint32_t dfgWidth,
                                        std::uint32_t dfgHeight, std::uint64_t& specularBytes,
                                        std::uint64_t& dfgBytes) {
  specularBytes = 0;
  std::uint64_t mipSize = faceSize;
  for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
    const std::uint64_t texels = mipSize * mipSize;
    if (texels > std::numeric_limits<std::uint64_t>::max() / 48ULL) return false;
    const std::uint64_t levelBytes = texels * 48ULL;  // six faces * RGBA * binary16
    if (specularBytes > std::numeric_limits<std::uint64_t>::max() - levelBytes) return false;
    specularBytes += levelBytes;
    mipSize = mipSize > 1 ? mipSize / 2 : 1;
  }
  const std::uint64_t dfgTexels = static_cast<std::uint64_t>(dfgWidth) * dfgHeight;
  if (dfgTexels > std::numeric_limits<std::uint64_t>::max() / 4ULL) return false;
  dfgBytes = dfgTexels * 4ULL;
  return true;
}

[[nodiscard]] bool halfIsFinite(std::uint16_t bits) { return (bits & 0x7C00U) != 0x7C00U; }

}  // namespace

std::vector<std::byte> encodeEnvironmentArtifact(AssetId assetId, const EnvironmentAssetData& data) {
  const std::uint32_t shOffset = static_cast<std::uint32_t>(kEnvironmentArtifactHeaderSizeBytes);
  const std::uint32_t shSize = static_cast<std::uint32_t>(kEnvironmentShPayloadSizeBytes);
  const std::uint32_t specularOffset = shOffset + shSize;
  const std::uint32_t specularSize = static_cast<std::uint32_t>(data.specularRgba16Float.size() * sizeof(std::uint16_t));
  const std::uint32_t dfgOffset = specularOffset + specularSize;
  const std::uint32_t dfgSize = static_cast<std::uint32_t>(data.dfgRg16Float.size() * sizeof(std::uint16_t));

  std::vector<std::byte> out;
  out.reserve(static_cast<std::size_t>(dfgOffset) + dfgSize);
  for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
  appendU32LE(out, kEnvironmentArtifactSchemaVersion);
  appendU64LE(out, assetId);
  appendU32LE(out, data.faceSize);
  appendU32LE(out, data.mipCount);
  appendU32LE(out, data.dfgWidth);
  appendU32LE(out, data.dfgHeight);
  appendU32LE(out, shOffset);
  appendU32LE(out, shSize);
  appendU32LE(out, specularOffset);
  appendU32LE(out, specularSize);
  appendU32LE(out, dfgOffset);
  appendU32LE(out, dfgSize);
  for (float coefficient : data.irradianceSh) appendFloatLE(out, coefficient);
  for (std::uint16_t value : data.specularRgba16Float) appendU16LE(out, value);
  for (std::uint16_t value : data.dfgRg16Float) appendU16LE(out, value);
  return out;
}

atlantis::Result<DecodedEnvironmentArtifact, EnvironmentArtifactDecodeError> decodeEnvironmentArtifact(
    const std::vector<std::byte>& bytes) {
  using ResultT = atlantis::Result<DecodedEnvironmentArtifact, EnvironmentArtifactDecodeError>;
  if (bytes.size() < kEnvironmentArtifactHeaderSizeBytes) {
    return ResultT::Err(EnvironmentArtifactDecodeError::TruncatedHeader);
  }
  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (bytes[i] != static_cast<std::byte>(kMagic[i])) return ResultT::Err(EnvironmentArtifactDecodeError::BadMagic);
  }
  if (readU32LE(bytes.data() + 8) != kEnvironmentArtifactSchemaVersion) {
    return ResultT::Err(EnvironmentArtifactDecodeError::UnsupportedSchemaVersion);
  }

  const std::uint32_t faceSize = readU32LE(bytes.data() + 20);
  const std::uint32_t mipCount = readU32LE(bytes.data() + 24);
  const std::uint32_t dfgWidth = readU32LE(bytes.data() + 28);
  const std::uint32_t dfgHeight = readU32LE(bytes.data() + 32);
  if (!isPowerOfTwo(faceSize) || !isPowerOfTwo(dfgWidth) || !isPowerOfTwo(dfgHeight) ||
      faceSize > kMaxEnvironmentDimension || dfgWidth > kMaxEnvironmentDimension ||
      dfgHeight > kMaxEnvironmentDimension) {
    return ResultT::Err(EnvironmentArtifactDecodeError::InvalidDimensions);
  }
  if (mipCount != fullMipCount(faceSize)) return ResultT::Err(EnvironmentArtifactDecodeError::InvalidMipCount);

  std::uint64_t expectedSpecularSize = 0;
  std::uint64_t expectedDfgSize = 0;
  if (!checkedExpectedSizes(faceSize, mipCount, dfgWidth, dfgHeight, expectedSpecularSize, expectedDfgSize) ||
      expectedSpecularSize > std::numeric_limits<std::uint32_t>::max() ||
      expectedDfgSize > std::numeric_limits<std::uint32_t>::max()) {
    return ResultT::Err(EnvironmentArtifactDecodeError::SizeOverflow);
  }

  const std::uint32_t shOffset = readU32LE(bytes.data() + 36);
  const std::uint32_t shSize = readU32LE(bytes.data() + 40);
  const std::uint32_t specularOffset = readU32LE(bytes.data() + 44);
  const std::uint32_t specularSize = readU32LE(bytes.data() + 48);
  const std::uint32_t dfgOffset = readU32LE(bytes.data() + 52);
  const std::uint32_t dfgSize = readU32LE(bytes.data() + 56);
  const std::uint64_t expectedShOffset = kEnvironmentArtifactHeaderSizeBytes;
  const std::uint64_t expectedSpecularOffset = expectedShOffset + kEnvironmentShPayloadSizeBytes;
  const std::uint64_t expectedDfgOffset = expectedSpecularOffset + expectedSpecularSize;
  const std::uint64_t expectedTotalSize = expectedDfgOffset + expectedDfgSize;
  if (shOffset != expectedShOffset || shSize != kEnvironmentShPayloadSizeBytes ||
      specularOffset != expectedSpecularOffset || specularSize != expectedSpecularSize || dfgOffset != expectedDfgOffset ||
      dfgSize != expectedDfgSize || bytes.size() != expectedTotalSize) {
    return ResultT::Err(EnvironmentArtifactDecodeError::InconsistentPayloadLayout);
  }

  DecodedEnvironmentArtifact decoded;
  decoded.assetId = readU64LE(bytes.data() + 12);
  decoded.data.faceSize = faceSize;
  decoded.data.mipCount = mipCount;
  decoded.data.dfgWidth = dfgWidth;
  decoded.data.dfgHeight = dfgHeight;
  for (std::size_t i = 0; i < decoded.data.irradianceSh.size(); ++i) {
    const float value = std::bit_cast<float>(readU32LE(bytes.data() + shOffset + i * sizeof(float)));
    if (!std::isfinite(value)) return ResultT::Err(EnvironmentArtifactDecodeError::NonFiniteValue);
    decoded.data.irradianceSh[i] = value;
  }

  decoded.data.specularRgba16Float.reserve(specularSize / sizeof(std::uint16_t));
  for (std::uint32_t offset = 0; offset < specularSize; offset += 2) {
    const std::uint16_t value = readU16LE(bytes.data() + specularOffset + offset);
    if (!halfIsFinite(value)) return ResultT::Err(EnvironmentArtifactDecodeError::NonFiniteValue);
    decoded.data.specularRgba16Float.push_back(value);
  }
  decoded.data.dfgRg16Float.reserve(dfgSize / sizeof(std::uint16_t));
  for (std::uint32_t offset = 0; offset < dfgSize; offset += 2) {
    const std::uint16_t value = readU16LE(bytes.data() + dfgOffset + offset);
    if (!halfIsFinite(value)) return ResultT::Err(EnvironmentArtifactDecodeError::NonFiniteValue);
    decoded.data.dfgRg16Float.push_back(value);
  }
  return ResultT::Ok(std::move(decoded));
}

}  // namespace atlantis::asset_system
