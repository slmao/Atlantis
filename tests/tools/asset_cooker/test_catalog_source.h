#pragma once

// Plan 0047 M3 (P7): a per-asset cook takes its GUID from the catalog
// source, so cooker tests that cook from a scratch or fixture asset root
// write a catalog source covering it. Each GUID is derived from the file's
// logical path, so it is the same on every run.

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::tools::asset_cooker::test {

[[nodiscard]] inline atlantis::asset_system::AssetGuid testCatalogGuid(std::string_view logicalPath) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), logicalPath);
}

[[nodiscard]] inline std::optional<atlantis::asset_system::CatalogAssetType> catalogTypeOf(std::string_view path) {
  using atlantis::asset_system::CatalogAssetType;
  const auto endsWith = [path](std::string_view suffix) {
    return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
  };
  if (endsWith(".mesh.txt")) return CatalogAssetType::Mesh;
  if (endsWith(".scene.txt")) return CatalogAssetType::Scene;
  if (endsWith(".material.txt")) return CatalogAssetType::Material;
  if (endsWith(".png") || endsWith(".dds")) return CatalogAssetType::Texture;
  if (endsWith(".hdr")) return CatalogAssetType::Environment;
  return std::nullopt;
}

// Writes, at catalogPath, a catalog source with one entry per asset source
// file currently under assetRoot (typed by extension). Returns catalogPath.
inline std::filesystem::path writeCatalogSourceCovering(const std::filesystem::path& assetRoot,
                                                        const std::filesystem::path& catalogPath) {
  std::vector<atlantis::asset_system::CatalogSourceEntry> entries;
  std::error_code ec;
  if (std::filesystem::is_directory(assetRoot, ec)) {
    for (const auto& file : std::filesystem::recursive_directory_iterator(assetRoot)) {
      if (!file.is_regular_file()) continue;
      const std::string logicalPath = std::filesystem::relative(file.path(), assetRoot).generic_string();
      const auto type = catalogTypeOf(logicalPath);
      if (!type) continue;
      entries.push_back({testCatalogGuid(logicalPath), *type, atlantis::asset_system::CatalogRoot::Assets, logicalPath});
    }
  }
  std::filesystem::create_directories(catalogPath.parent_path());
  std::ofstream(catalogPath, std::ios::binary) << atlantis::asset_system::serializeAssetCatalogSource(entries);
  return catalogPath;
}

}  // namespace atlantis::tools::asset_cooker::test
