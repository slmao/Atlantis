#include <atlantis/asset_system/cook.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/mesh_source.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace atlantis::asset_system {

namespace {

namespace fs = std::filesystem;

// Plan 0012 Section D10: write-to-temp-then-rename() in the same
// directory as finalPath. std::filesystem::rename() is required by the
// C++17 standard to atomically replace finalPath if it already exists,
// so a reader only ever observes the fully-old or fully-new file, never
// a partial write. On any failure, the temp file is removed on a
// best-effort basis and any pre-existing, previously-valid finalPath is
// left completely untouched.
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

atlantis::Result<std::monostate, CookError> cookStaticMesh(const std::string& sourceFilePath,
                                                             const std::string& logicalPathInput,
                                                             const std::string& artifactOutputPath,
                                                             const std::string& metadataOutputPath) {
  using ResultT = atlantis::Result<std::monostate, CookError>;

  const auto normalizedResult = normalizeLogicalPath(logicalPathInput);
  if (normalizedResult.isErr()) return ResultT::Err(CookError::LogicalPathInvalid);
  const std::string& normalizedLogicalPath = normalizedResult.value();

  std::ifstream sourceFile(sourceFilePath, std::ios::binary);
  if (!sourceFile.is_open()) return ResultT::Err(CookError::SourceFileUnreadable);
  std::ostringstream sourceStream;
  sourceStream << sourceFile.rdbuf();
  if (sourceFile.bad()) return ResultT::Err(CookError::SourceFileUnreadable);
  const std::string sourceText = sourceStream.str();

  const auto parsedResult = parseMeshSource(sourceText);
  if (parsedResult.isErr()) return ResultT::Err(CookError::SourceParseFailed);
  const ParsedMeshSource& parsed = parsedResult.value();

  const AssetId assetId = computeAssetId(normalizedLogicalPath);
  const std::vector<std::byte> artifactBytes = encodeMeshArtifact(assetId, parsed);

  AssetMetadata metadata;
  metadata.assetId = assetId;
  metadata.sourceLogicalPath = normalizedLogicalPath;
  metadata.importerVersion = std::string(kImporterVersion);
  metadata.assetType = "static_mesh";
  metadata.vertexCount = static_cast<std::uint32_t>(parsed.vertices.size());
  metadata.indexCount = static_cast<std::uint32_t>(parsed.indices.size());
  metadata.vertexStrideBytes = kMeshArtifactVertexStrideBytes;
  const std::string metadataText = serializeAssetMetadata(metadata);

  if (!writeBytesAtomically(artifactOutputPath, reinterpret_cast<const char*>(artifactBytes.data()),
                             artifactBytes.size())) {
    return ResultT::Err(CookError::ArtifactWriteFailed);
  }
  if (!writeTextAtomically(metadataOutputPath, metadataText)) {
    return ResultT::Err(CookError::MetadataWriteFailed);
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system
