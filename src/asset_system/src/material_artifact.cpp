#include <atlantis/asset_system/material_artifact.h>

#include <array>
#include <bit>
#include <cmath>

namespace atlantis::asset_system {

namespace {

constexpr std::array<char, 8> kMagic = {'A', 'T', 'L', 'M', 'A', 'T', '\0', '\0'};

void appendU32LE(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

void appendU64LE(std::vector<std::byte>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

// Reinterprets the IEEE-754 bit pattern via std::bit_cast (same-size,
// same-machine reinterpretation, numerically correct regardless of host
// endianness), then serializes that pattern via the identical shift/mask
// routine every other integer field already uses -- never a memcpy of
// the float itself. Matches mesh_artifact.cpp's own identical
// appendFloatLE()/readFloatLE() precedent exactly.
void appendFloatLE(std::vector<std::byte>& out, float value) { appendU32LE(out, std::bit_cast<std::uint32_t>(value)); }

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

[[nodiscard]] float readFloatLE(const std::byte* bytes) { return std::bit_cast<float>(readU32LE(bytes)); }

[[nodiscard]] std::uint32_t kindToField(MaterialKind kind) {
  switch (kind) {
    case MaterialKind::UnlitTextured:
      return 0;
    case MaterialKind::LitTextured:
      return 1;
    case MaterialKind::PbrDirectLit:
      return 2;
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
                                               MaterialSamplerAddressMode addressMode,
                                               const float (&baseColorFactor)[4], float metallicFactor,
                                               float roughnessFactor) {
  std::vector<std::byte> out;
  out.reserve(kMaterialArtifactHeaderSizeBytes);

  for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
  appendU32LE(out, kMaterialArtifactSchemaVersion);
  appendU32LE(out, kindToField(kind));
  appendU64LE(out, textureAsset);
  appendU32LE(out, filterToField(filter));
  appendU32LE(out, addressModeToField(addressMode));
  for (float component : baseColorFactor) appendFloatLE(out, component);
  appendFloatLE(out, metallicFactor);
  appendFloatLE(out, roughnessFactor);

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
  } else if (kindField == 1) {
    decoded.kind = MaterialKind::LitTextured;
  } else if (kindField == 2) {
    decoded.kind = MaterialKind::PbrDirectLit;
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

  // Plan 0023 Milestone 1 (ADR-0066 item 5): independently re-validated
  // here against the artifact's own decoded bytes, never trusted from a
  // well-formed cooker output alone -- mirrors cookMaterial()'s own
  // identical cook-time check.
  for (std::size_t i = 0; i < 4; ++i) {
    const float component = readFloatLE(bytes.data() + 32 + (i * 4));
    if (!std::isfinite(component) || component < 0.0f || component > 1.0f) {
      return ResultT::Err(MaterialArtifactDecodeError::BaseColorFactorOutOfRange);
    }
    decoded.baseColorFactor[i] = component;
  }

  const float metallicFactor = readFloatLE(bytes.data() + 48);
  if (!std::isfinite(metallicFactor) || metallicFactor < 0.0f || metallicFactor > 1.0f) {
    return ResultT::Err(MaterialArtifactDecodeError::MaterialFactorOutOfRange);
  }
  decoded.metallicFactor = metallicFactor;

  const float roughnessFactor = readFloatLE(bytes.data() + 52);
  if (!std::isfinite(roughnessFactor) || roughnessFactor < 0.0f || roughnessFactor > 1.0f) {
    return ResultT::Err(MaterialArtifactDecodeError::MaterialFactorOutOfRange);
  }
  decoded.roughnessFactor = roughnessFactor;

  return ResultT::Ok(std::move(decoded));
}

}  // namespace atlantis::asset_system
