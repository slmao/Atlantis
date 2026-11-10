#include "import_command.h"

#include <atlantis/asset_system/asset_catalog_source.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int wmain(int argc, wchar_t** argv) {
  namespace as = atlantis::asset_system;
  namespace importer = atlantis::gltf_importer;
  namespace fs = std::filesystem;
  if (argc != 4) return 1;
  std::ifstream catalog{fs::path(argv[1]), std::ios::binary};
  const std::string text((std::istreambuf_iterator<char>(catalog)), {});
  const auto source = as::parseAssetCatalogSource(text);
  if (source.isErr()) {
    std::cerr << "Invalid saved-slot catalog source\n";
    return 1;
  }
  const auto* scene = source.value().find(as::CatalogRoot::Assets, fs::path(argv[2]).generic_string());
  if (!scene || scene->type != as::CatalogAssetType::Scene) return 1;
  const auto key = as::assetKey(scene->guid);
  if (key == 0) {
    std::cerr << "Saved scene has a zero AssetId\n";
    return 1;
  }
  for (const auto& entry : source.value().entries()) {
    if (&entry != scene && as::assetKey(entry.guid) == key) {
      std::cerr << "Saved scene identity conflicts with catalog input " << entry.path << '\n';
      return 1;
    }
  }

  std::ifstream inputs{fs::path(argv[3])};
  if (!inputs) return 1;
  for (std::string line; std::getline(inputs, line);) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) return 1;
    const auto owner = as::parseAssetGuid(std::string_view(line).substr(0, tab));
    if (owner.isErr()) return 1;
    const auto path = fs::path(std::u8string(line.begin() + static_cast<std::ptrdiff_t>(tab + 1), line.end()));
    const auto generated = importer::importAssetGuids(path, owner.value());
    if (generated.isErr()) {
      std::cerr << "Cannot read generated asset identities for " << path << ": "
                << importer::gltfImportErrorMessage(generated.error()) << '\n';
      return 1;
    }
    for (const auto& guid : generated.value()) {
      if (guid == scene->guid || as::assetKey(guid) == key) {
        std::cerr << "Saved scene identity conflicts with generated asset " << as::toString(guid)
                  << " from " << path << '\n';
        return 1;
      }
    }
  }
  return inputs.bad() ? 1 : 0;
}
