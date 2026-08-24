#include <atlantis/asset_system/texture_artifact.h>

#include <array>

namespace atlantis::asset_system {

namespace {

constexpr std::array<char, 8> kMagic = {'A', 'T', 'L', 'T', 'E', 'X', '\0', '\0'};

void appendU32LE(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

[[nodiscard]] std::uint32_t readU32LE(const std::byte* bytes) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
  return value;
}

[[nodiscard]] std::uint32_t colorSpaceToFormatField(TextureColorSpace colorSpace) {
  switch (colorSpace) {
    case TextureColorSpace::Unorm:
      return 0;
    case TextureColorSpace::Srgb:
      return 1;
  }
  return 0;
}

}  // namespace

std::vector<std::byte> encodeTextureArtifact(std::uint32_t width, std::uint32_t height, TextureColorSpace colorSpace,
                                              const std::uint8_t* pixelBytes, std::size_t pixelByteCount) {
  std::vector<std::byte> out;
  out.reserve(kTextureArtifactHeaderSizeBytes + pixelByteCount);

  for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
  appendU32LE(out, kTextureArtifactSchemaVersion);
  appendU32LE(out, width);
  appendU32LE(out, height);
  appendU32LE(out, colorSpaceToFormatField(colorSpace));
  appendU32LE(out, 1);  // mipCount -- always 1 this round
  appendU32LE(out, static_cast<std::uint32_t>(kTextureArtifactHeaderSizeBytes));
  appendU32LE(out, static_cast<std::uint32_t>(pixelByteCount));

  for (std::size_t i = 0; i < pixelByteCount; ++i) out.push_back(static_cast<std::byte>(pixelBytes[i]));

  return out;
}

atlantis::Result<DecodedTextureArtifact, TextureArtifactDecodeError> decodeTextureArtifact(
    const std::vector<std::byte>& bytes) {
  using ResultT = atlantis::Result<DecodedTextureArtifact, TextureArtifactDecodeError>;

  if (bytes.size() < kTextureArtifactHeaderSizeBytes) return ResultT::Err(TextureArtifactDecodeError::TruncatedHeader);

  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (bytes[i] != static_cast<std::byte>(kMagic[i])) return ResultT::Err(TextureArtifactDecodeError::BadMagic);
  }

  const std::uint32_t schemaVersion = readU32LE(bytes.data() + 8);
  if (schemaVersion != kTextureArtifactSchemaVersion) {
    return ResultT::Err(TextureArtifactDecodeError::UnsupportedSchemaVersion);
  }

  const std::uint32_t width = readU32LE(bytes.data() + 12);
  const std::uint32_t height = readU32LE(bytes.data() + 16);
  if (width == 0 || width > kMaxTextureDimension || height == 0 || height > kMaxTextureDimension) {
    return ResultT::Err(TextureArtifactDecodeError::DimensionExceedsMaximum);
  }

  const std::uint32_t formatField = readU32LE(bytes.data() + 20);
  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  if (formatField == 0) {
    colorSpace = TextureColorSpace::Unorm;
  } else if (formatField == 1) {
    colorSpace = TextureColorSpace::Srgb;
  } else {
    return ResultT::Err(TextureArtifactDecodeError::UnknownFormat);
  }

  const std::uint32_t mipCount = readU32LE(bytes.data() + 24);
  if (mipCount != 1) return ResultT::Err(TextureArtifactDecodeError::UnsupportedMipCount);

  const std::uint32_t pixelDataOffset = readU32LE(bytes.data() + 28);
  const std::uint32_t pixelDataSizeBytes = readU32LE(bytes.data() + 32);

  // Every size computed in uint64_t before comparison, so a header
  // crafted to overflow a 32-bit product cannot drive an oversized or
  // wrapped-around allocation -- width/height are already bounded above,
  // so this product cannot itself overflow uint64_t.
  const auto expectedPixelDataOffset = static_cast<std::uint64_t>(kTextureArtifactHeaderSizeBytes);
  const std::uint64_t expectedPixelDataSizeBytes =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;

  if (static_cast<std::uint64_t>(pixelDataOffset) != expectedPixelDataOffset ||
      static_cast<std::uint64_t>(pixelDataSizeBytes) != expectedPixelDataSizeBytes) {
    return ResultT::Err(TextureArtifactDecodeError::InconsistentPixelDataSize);
  }

  const std::uint64_t expectedTotalSize = expectedPixelDataOffset + expectedPixelDataSizeBytes;
  if (static_cast<std::uint64_t>(bytes.size()) != expectedTotalSize) {
    return ResultT::Err(TextureArtifactDecodeError::InconsistentPixelDataSize);
  }

  DecodedTextureArtifact decoded;
  decoded.width = width;
  decoded.height = height;
  decoded.colorSpace = colorSpace;
  decoded.pixelBytes.reserve(pixelDataSizeBytes);
  for (std::uint32_t i = 0; i < pixelDataSizeBytes; ++i) {
    decoded.pixelBytes.push_back(static_cast<std::uint8_t>(bytes[pixelDataOffset + i]));
  }

  return ResultT::Ok(std::move(decoded));
}

}  // namespace atlantis::asset_system
