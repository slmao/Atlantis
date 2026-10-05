#pragma once

// Plan 0047 M5: hand-authored asset catalogs for the Runtime's scene-load
// tests. Every record's locations are relative to the directory the catalog
// is written into, as loadAssetCatalog() requires.

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace atlantis::runtime::test_support {

// A deterministic, non-nil test identity per logical path, so a test's
// cross-references (scene -> mesh, material -> texture) agree. The same
// derivation each test file's own cook helpers use.
[[nodiscard]] inline atlantis::asset_system::AssetGuid catalogTestGuid(std::string_view key) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), key);
}

class CatalogBuilder {
 public:
  explicit CatalogBuilder(const std::filesystem::path& dir) : dir_(dir) {}

  CatalogBuilder& add(atlantis::asset_system::CatalogAssetType type, const std::string& logicalPath,
                      const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath,
                      std::uint32_t artifactSchema,
                      std::vector<atlantis::asset_system::AssetGuid> dependencies = {}) {
    atlantis::asset_system::AssetCatalogRecord record;
    record.guid = catalogTestGuid(logicalPath);
    record.assetId = atlantis::asset_system::assetKey(record.guid);
    record.type = type;
    record.source = {atlantis::asset_system::CatalogRoot::Assets, logicalPath, ""};
    record.artifact = std::filesystem::relative(artifactPath, dir_).generic_string();
    record.metadata = std::filesystem::relative(metadataPath, dir_).generic_string();
    record.artifactSchema = artifactSchema;
    record.tool = "test";
    record.dependencies = std::move(dependencies);
    records_.push_back(std::move(record));
    return *this;
  }

  CatalogBuilder& addMesh(const std::string& logicalPath, const std::filesystem::path& artifactPath,
                          const std::filesystem::path& metadataPath,
                          std::uint32_t schema = atlantis::asset_system::kMeshArtifactSchemaVersion) {
    return add(atlantis::asset_system::CatalogAssetType::Mesh, logicalPath, artifactPath, metadataPath, schema);
  }

  CatalogBuilder& addTexture(const std::string& logicalPath, const std::filesystem::path& artifactPath,
                             const std::filesystem::path& metadataPath) {
    return add(atlantis::asset_system::CatalogAssetType::Texture, logicalPath, artifactPath, metadataPath,
               atlantis::asset_system::kTextureArtifactSchemaVersion);
  }

  CatalogBuilder& addMaterial(const std::string& logicalPath, const std::filesystem::path& artifactPath,
                              const std::filesystem::path& metadataPath,
                              std::vector<atlantis::asset_system::AssetGuid> dependencies = {}) {
    return add(atlantis::asset_system::CatalogAssetType::Material, logicalPath, artifactPath, metadataPath,
               atlantis::asset_system::kMaterialArtifactSchemaVersion, std::move(dependencies));
  }

  // `scene` is any fixture with artifactPath/metadataPath members.
  template <typename SceneFixture>
  CatalogBuilder& addScene(const std::string& logicalPath, const SceneFixture& scene,
                           std::vector<atlantis::asset_system::AssetGuid> dependencies) {
    return add(atlantis::asset_system::CatalogAssetType::Scene, logicalPath, scene.artifactPath, scene.metadataPath,
               atlantis::asset_system::kSceneArtifactSchemaVersion, std::move(dependencies));
  }

  // Writes <dir>/catalog.txt and returns its path.
  [[nodiscard]] std::filesystem::path write() const {
    const std::filesystem::path path = dir_ / "catalog.txt";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << atlantis::asset_system::serializeAssetCatalog(records_);
    return path;
  }

 private:
  std::filesystem::path dir_;
  std::vector<atlantis::asset_system::AssetCatalogRecord> records_;
};

}  // namespace atlantis::runtime::test_support
