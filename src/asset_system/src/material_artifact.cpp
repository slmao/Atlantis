#include <atlantis/asset_system/material_artifact.h>

#include <array>

namespace atlantis::asset_system {

namespace {

constexpr std::array<char, 8> kMagic = {'A', 'T', 'L', 'M', 'A', 'T', '\0', '\0'};

void appendU32LE(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

void appendU64LE(std::vector<std::byte>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
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

[[nodiscard]] std::uint32_t kindToField(MaterialKind kind) {
  switch (kind) {
    case MaterialKind::UnlitTextured:
      return 0;
  }
  return 0;
}

[[nodiscard]] std::uint32_t filterToField(MaterialSamplerFilter filter) {
  switch (filter) {
    case MaterialSamplerFilter::Nearest:
      return 0;
    case MaterialSamplerFilter::Linear:
      return 1;
  }
  return 1;
}

[[nodiscard]] std::uint32_t addressModeToField(MaterialSamplerAddressMode addressMode) {
  switch (addressMode) {
    case MaterialSamplerAddressMode::Repeat:
      return 0;
    case MaterialSamplerAddressMode::ClampToEdge:
      return 1;
  }
  return 0;
}

}  // namespace

std::vector<std::byte> encodeMaterialArtifact(MaterialKind kind, AssetId textureAsset, MaterialSamplerFilter filter,
                                               MaterialSamplerAddressMode addressMode) {
  std::vector<std::byte> out;
  out.reserve(kMaterialArtifactHeaderSizeBytes);

  for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
  appendU32LE(out, kMaterialArtifactSchemaVersion);
  appendU32LE(out, kindToField(kind));
  appendU64LE(out, textureAsset);
  appendU32LE(out, filterToField(filter));
  appendU32LE(out, addressModeToField(addressMode));

  return out;
}

atlantis::Result<DecodedMaterialArtifact, MaterialArtifactDecodeError> decodeMaterialArtifact(
    const std::vector<std::byte>& bytes) {
  using ResultT = atlantis::Result<DecodedMaterialArtifact, MaterialArtifactDecodeError>;

  if (bytes.size() < kMaterialArtifactHeaderSizeBytes) {
    return ResultT::Err(MaterialArtifactDecodeError::TruncatedHeader);
  }
  if (bytes.size() > kMaterialArtifactHeaderSizeBytes) {
    return ResultT::Err(MaterialArtifactDecodeError::UnexpectedSize);
  }

  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (bytes[i] != static_cast<std::byte>(kMagic[i])) return ResultT::Err(MaterialArtifactDecodeError::BadMagic);
  }

  const std::uint32_t schemaVersion = readU32LE(bytes.data() + 8);
  if (schemaVersion != kMaterialArtifactSchemaVersion) {
    return ResultT::Err(MaterialArtifactDecodeError::UnsupportedSchemaVersion);
  }

  DecodedMaterialArtifact decoded;

  const std::uint32_t kindField = readU32LE(bytes.data() + 12);
  if (kindField == 0) {
    decoded.kind = MaterialKind::UnlitTextured;
  } else {
    return ResultT::Err(MaterialArtifactDecodeError::UnknownMaterialKind);
  }

  decoded.textureAsset = readU64LE(bytes.data() + 16);

  const std::uint32_t filterField = readU32LE(bytes.data() + 24);
  if (filterField == 0) {
    decoded.filter = MaterialSamplerFilter::Nearest;
  } else if (filterField == 1) {
    decoded.filter = MaterialSamplerFilter::Linear;
  } else {
    return ResultT::Err(MaterialArtifactDecodeError::UnknownFilter);
  }

  const std::uint32_t addressModeField = readU32LE(bytes.data() + 28);
  if (addressModeField == 0) {
    decoded.addressMode = MaterialSamplerAddressMode::Repeat;
  } else if (addressModeField == 1) {
    decoded.addressMode = MaterialSamplerAddressMode::ClampToEdge;
  } else {
    return ResultT::Err(MaterialArtifactDecodeError::UnknownAddressMode);
  }

  return ResultT::Ok(std::move(decoded));
}

}  // namespace atlantis::asset_system
