#include <atlantis/asset_system/cook_material.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/material_metadata.h>
#include <atlantis/asset_system/material_source.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace atlantis::asset_system {

namespace {

namespace fs = std::filesystem;

// Duplicated from cook_scene.cpp's own identical helpers rather than
// shared, matching that file's own file-local, not-exported precedent
// for exactly this class of helper.
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

atlantis::Result<std::monostate, MaterialCookError> cookMaterial(const std::string& sourceFilePath,
                                                                   const std::string& logicalPathInput,
                                                                   const std::string& artifactOutputPath,
                                                                   const std::string& metadataOutputPath) {
  using ResultT = atlantis::Result<std::monostate, MaterialCookError>;

  // Step 1: read + parse.
  std::ifstream sourceFile(sourceFilePath, std::ios::binary);
  if (!sourceFile.is_open()) return ResultT::Err(MaterialCookError::SourceFileUnreadable);
  std::ostringstream sourceStream;
  sourceStream << sourceFile.rdbuf();
  if (sourceFile.bad()) return ResultT::Err(MaterialCookError::SourceFileUnreadable);
  const std::string sourceText = sourceStream.str();

  const auto parsedResult = parseMaterialSource(sourceText);
  if (parsedResult.isErr()) return ResultT::Err(MaterialCookError::SourceParseFailed);
  const ParsedMaterialSource& parsed = parsedResult.value();

  // Step 2: this material's own identity.
  const auto normalizedSelfResult = normalizeLogicalPath(logicalPathInput);
  if (normalizedSelfResult.isErr()) return ResultT::Err(MaterialCookError::LogicalPathInvalid);
  const std::string& normalizedSelfPath = normalizedSelfResult.value();
  const AssetId selfAssetId = computeAssetId(normalizedSelfPath);

  // Step 3: the referenced texture's own identity -- value-level only,
  // never an existence check (ADR-0059 D6/D7).
  const auto normalizedTextureResult = normalizeLogicalPath(parsed.textureLogicalPath);
  if (normalizedTextureResult.isErr()) return ResultT::Err(MaterialCookError::LogicalPathInvalid);
  const AssetId textureAssetId = computeAssetId(normalizedTextureResult.value());

  // Step 4: encode + atomic write.
  const std::vector<std::byte> artifactBytes =
      encodeMaterialArtifact(parsed.kind, textureAssetId, parsed.filter, parsed.addressMode);

  MaterialMetadata metadata;
  metadata.assetId = selfAssetId;
  metadata.sourceLogicalPath = normalizedSelfPath;
  metadata.kind = parsed.kind;
  metadata.textureAsset = textureAssetId;
  const std::string metadataText = serializeMaterialMetadata(metadata);

  if (!writeBytesAtomically(artifactOutputPath, reinterpret_cast<const char*>(artifactBytes.data()),
                             artifactBytes.size())) {
    return ResultT::Err(MaterialCookError::AtomicWriteFailed);
  }
  if (!writeTextAtomically(metadataOutputPath, metadataText)) {
    return ResultT::Err(MaterialCookError::AtomicWriteFailed);
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system
