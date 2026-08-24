#include <atlantis/asset_system/cook_texture.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

namespace atlantis::asset_system {

namespace {

namespace fs = std::filesystem;

// Duplicated from cook.cpp/cook_scene.cpp's own identical helpers rather
// than shared, matching this module's own already-established
// duplication precedent (scene_metadata.cpp's own comment on this).
[[nodiscard]] bool writeBytesAtomically(const fs::path& finalPath, const char* data, std::size_t size) {
  std::error_code ec;
  const fs::path dir = finalPath.parent_path();
  if (!dir.empty()) {
    fs::create_directories(dir, ec);
  }

  std::random_device rd;
  const fs::path tempPath =
      dir / (finalPath.filename().string() + ".tmp-" + std::to_string(rd()) + std::to_string(rd()));

  {
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(data, static_cast<std::streamsize>(size));
    out.flush();
    if (!out.good()) {
      out.close();
      fs::remove(tempPath, ec);
      return false;
    }
  }

  fs::rename(tempPath, finalPath, ec);
  if (ec) {
    fs::remove(tempPath, ec);
    return false;
  }
  return true;
}

[[nodiscard]] bool writeTextAtomically(const fs::path& finalPath, const std::string& text) {
  return writeBytesAtomically(finalPath, text.data(), text.size());
}

}  // namespace

atlantis::Result<std::monostate, TextureCookError> cookTexture(const std::uint8_t* pixelBytes, std::uint32_t width,
                                                                 std::uint32_t height, std::int32_t channelsInFile,
                                                                 TextureColorSpace colorSpace,
                                                                 const std::string& logicalPathInput,
                                                                 const std::filesystem::path& artifactOutputPath,
                                                                 const std::filesystem::path& metadataOutputPath) {
  using ResultT = atlantis::Result<std::monostate, TextureCookError>;

  const auto normalizedResult = normalizeLogicalPath(logicalPathInput);
  if (normalizedResult.isErr()) return ResultT::Err(TextureCookError::LogicalPathInvalid);
  const std::string& normalizedLogicalPath = normalizedResult.value();

  if (width == 0 || height == 0) return ResultT::Err(TextureCookError::ZeroDimension);
  if (width > kMaxTextureDimension || height > kMaxTextureDimension) {
    return ResultT::Err(TextureCookError::DimensionExceedsMaximum);
  }

  // Checked (64-bit-before-32-bit) size arithmetic (Spec 0016 Human
  // Review item 9) -- structurally unreachable given the dimension bound
  // just above (8192*8192*4 = 268,435,456, well within uint32_t), kept
  // as an explicit defense-in-depth check rather than relying solely on
  // that bound never changing.
  const std::uint64_t pixelByteCount64 =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
  if (pixelByteCount64 > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return ResultT::Err(TextureCookError::SourceOverflow);
  }
  const auto pixelByteCount = static_cast<std::size_t>(pixelByteCount64);

  const AssetId assetId = computeAssetId(normalizedLogicalPath);
  const std::vector<std::byte> artifactBytes =
      encodeTextureArtifact(width, height, colorSpace, pixelBytes, pixelByteCount);

  TextureMetadata metadata;
  metadata.assetId = assetId;
  metadata.sourceLogicalPath = normalizedLogicalPath;
  metadata.width = width;
  metadata.height = height;
  metadata.format = colorSpace;
  metadata.channelsInFile = channelsInFile;
  const std::string metadataText = serializeTextureMetadata(metadata);

  if (!writeBytesAtomically(artifactOutputPath, reinterpret_cast<const char*>(artifactBytes.data()),
                             artifactBytes.size())) {
    return ResultT::Err(TextureCookError::AtomicWriteFailed);
  }
  if (!writeTextAtomically(metadataOutputPath, metadataText)) {
    return ResultT::Err(TextureCookError::AtomicWriteFailed);
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system
