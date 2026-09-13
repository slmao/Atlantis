#include <atlantis/asset_system/cook_material.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/material_metadata.h>
#include <atlantis/asset_system/material_source.h>

#include <cmath>
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

// Plan 0023 Milestone 1 (ADR-0066 item 5): finite and in [0, 1] --
// shared by both the four baseColorFactor components and the two
// scalar factors.
[[nodiscard]] bool isValidFactor(float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }

// Plan 0035 Milestone 4 (ADR-0081): anisotropyFactor's own valid range
// is [-1, 1] (Spec 0035's own field definition), NOT [0, 1] like every
// other factor isValidFactor() above checks -- a real, deliberate
// difference, not an oversight.
[[nodiscard]] bool isValidAnisotropyFactor(float value) {
  return std::isfinite(value) && value >= -1.0f && value <= 1.0f;
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

  // Step 3.4 (Plan 0029 Section P6/ADR-0074 Section 1): the optional
  // normal-map texture's own identity -- identical value-level-only
  // normalization, `0` when absent (the material grammar's own kind
  // restriction already guarantees this is empty for anything but
  // PbrDirectLit).
  AssetId normalMapTextureAssetId = 0;
  if (!parsed.normalMapLogicalPath.empty()) {
    const auto normalizedNormalMapResult = normalizeLogicalPath(parsed.normalMapLogicalPath);
    if (normalizedNormalMapResult.isErr()) return ResultT::Err(MaterialCookError::LogicalPathInvalid);
    normalMapTextureAssetId = computeAssetId(normalizedNormalMapResult.value());
  }

  // Step 3.5 (Plan 0023 Milestone 1, ADR-0066 item 5; Plan 0035
  // Milestone 2/ADR-0081 widening): value-range validation, both
  // directions -- never a naive parse-and-trust. clearcoatFactor/
  // clearcoatRoughness share MaterialFactorOutOfRange with metallic/
  // roughness (the same "a scalar material factor is out of range"
  // condition, not a new enumerator) -- parseMaterialSource() already
  // guarantees these two are only ever non-default for kind ==
  // PbrClearcoat, so validating them unconditionally here is harmless
  // for every other kind (still their own inert 0.0f default, always
  // in range).
  for (float component : parsed.baseColorFactor) {
    if (!isValidFactor(component)) return ResultT::Err(MaterialCookError::BaseColorFactorOutOfRange);
  }
  if (!isValidFactor(parsed.metallicFactor) || !isValidFactor(parsed.roughnessFactor)) {
    return ResultT::Err(MaterialCookError::MaterialFactorOutOfRange);
  }
  if (!isValidFactor(parsed.clearcoatFactor) || !isValidFactor(parsed.clearcoatRoughness)) {
    return ResultT::Err(MaterialCookError::MaterialFactorOutOfRange);
  }
  // Plan 0035 Milestone 3/ADR-0081 widening: sheenColor's three
  // components and sheenRoughness share MaterialFactorOutOfRange too,
  // mirroring clearcoatFactor/clearcoatRoughness's own identical
  // reasoning immediately above -- parseMaterialSource() already
  // guarantees these are only ever non-default for kind == PbrSheen.
  for (float component : parsed.sheenColor) {
    if (!isValidFactor(component)) return ResultT::Err(MaterialCookError::MaterialFactorOutOfRange);
  }
  if (!isValidFactor(parsed.sheenRoughness)) return ResultT::Err(MaterialCookError::MaterialFactorOutOfRange);
  // Plan 0035 Milestone 4/ADR-0081 widening: anisotropyFactor uses its
  // own [-1, 1] range check (isValidAnisotropyFactor, above);
  // anisotropyRotation is an unbounded angle in radians, only checked
  // for finiteness -- parseMaterialSource() already guarantees both are
  // only ever non-default for kind == PbrAnisotropic.
  if (!isValidAnisotropyFactor(parsed.anisotropyFactor)) {
    return ResultT::Err(MaterialCookError::MaterialFactorOutOfRange);
  }
  if (!std::isfinite(parsed.anisotropyRotation)) {
    return ResultT::Err(MaterialCookError::MaterialFactorOutOfRange);
  }

  // Step 4: encode + atomic write.
  const std::vector<std::byte> artifactBytes = encodeMaterialArtifact(
      parsed.kind, textureAssetId, parsed.filter, parsed.addressMode, parsed.baseColorFactor, parsed.metallicFactor,
      parsed.roughnessFactor, normalMapTextureAssetId, parsed.clearcoatFactor, parsed.clearcoatRoughness,
      parsed.sheenColor, parsed.sheenRoughness, parsed.anisotropyFactor, parsed.anisotropyRotation);

  MaterialMetadata metadata;
  metadata.assetId = selfAssetId;
  metadata.sourceLogicalPath = normalizedSelfPath;
  metadata.kind = parsed.kind;
  metadata.textureAsset = textureAssetId;
  for (std::size_t i = 0; i < 4; ++i) metadata.baseColorFactor[i] = parsed.baseColorFactor[i];
  metadata.metallicFactor = parsed.metallicFactor;
  metadata.roughnessFactor = parsed.roughnessFactor;
  metadata.normalMapTexture = normalMapTextureAssetId;
  metadata.clearcoatFactor = parsed.clearcoatFactor;
  metadata.clearcoatRoughness = parsed.clearcoatRoughness;
  for (std::size_t i = 0; i < 3; ++i) metadata.sheenColor[i] = parsed.sheenColor[i];
  metadata.sheenRoughness = parsed.sheenRoughness;
  metadata.anisotropyFactor = parsed.anisotropyFactor;
  metadata.anisotropyRotation = parsed.anisotropyRotation;
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
