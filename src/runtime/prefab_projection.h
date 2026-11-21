#pragma once

#include "static_group_projection.h"

#include <atlantis/connection/authoring_connection.h>
#include <atlantis/asset_system/scene_source.h>

#include <map>
#include <set>
#include <span>

namespace atlantis::runtime::prefabs {
namespace as = asset_system;
namespace ac = connection::assets;
namespace fs = std::filesystem;

inline std::optional<std::string> nameOf(const as::AssetCatalogRecord& record) {
  constexpr std::string_view prefix = "prefabs/prefab_";
  constexpr std::string_view suffix = "/source.scene.txt";
  const std::string_view path = record.source.path;
  if (record.type != as::CatalogAssetType::Scene || record.source.root != as::CatalogRoot::Assets ||
      !record.source.subKey.empty() || !path.starts_with(prefix) || !path.ends_with(suffix) ||
      path.size() <= prefix.size() + suffix.size()) return std::nullopt;
  const auto name = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
  return connection::authoring::validSaveAsName(name) ? std::optional<std::string>(name) : std::nullopt;
}

inline std::optional<std::set<as::AssetGuid>> dependencies(const as::ParsedSceneSource& source,
                                                         const as::AssetCatalog& catalog) {
  if (source.activeCameraNodeId || source.nodes.empty() || source.nodes.size() > 64) return std::nullopt;
  std::set<as::AssetGuid> ids;
  std::size_t roots = 0;
  bool renderable = false;
  for (const auto& node : source.nodes) {
    if (!node.parentNodeId) ++roots;
    if (node.camera || node.light || node.prefabOrigin || (node.materialAsset && !node.meshAsset)) return std::nullopt;
    const auto resolve = [&](const auto& guid, as::CatalogAssetType type) {
      if (!guid) return true;
      const auto* record = catalog.find(*guid);
      if (!record || record->type != type) return false;
      ids.insert(*guid);
      return true;
    };
    if (!resolve(node.meshAsset, as::CatalogAssetType::Mesh) || !resolve(node.materialAsset, as::CatalogAssetType::Material))
      return std::nullopt;
    renderable |= node.meshAsset.has_value();
  }
  return roots == 1 && renderable ? std::optional{std::move(ids)} : std::nullopt;
}

inline std::optional<std::string> read(const fs::path& root, const std::string& path) {
  std::string bytes(ac::kStaticGroupByteLimit, '\0');
  std::size_t remaining = bytes.size(), size = 0;
  if (!static_groups::readBounded(root, path, bytes.data(), remaining, size)) return std::nullopt;
  bytes.resize(size);
  return bytes;
}

inline ac::StaticGroupResult project(const fs::path& root, const as::AssetCatalog& catalog, const as::AssetGuid& guid) {
  using R = ac::StaticGroupResult;
  using E = ac::StaticGroupError;
  const auto* record = catalog.find(guid);
  if (!record) return R::Err(E::UnknownAsset);
  auto name = nameOf(*record);
  if (!name) return R::Err(E::UnsupportedGroup);
  const auto sourcePath = root / "assets" / static_groups::pathFromUtf8(record->source.path);
  const auto sourceUtf8 = sourcePath.generic_u8string();
  const auto text = read(root, {sourceUtf8.begin(), sourceUtf8.end()});
  if (!text) return R::Err(E::InvalidGroup);
  const auto parsed = as::parseSceneSource(*text);
  if (parsed.isErr() || as::serializeSceneSource(parsed.value()) != *text) return R::Err(E::InvalidGroup);
  const auto ids = dependencies(parsed.value(), catalog);
  if (!ids || !std::ranges::equal(*ids, record->dependencies)) return R::Err(E::InvalidGroup);
  const auto cooked = as::cookSceneSource(*text, guid);
  const auto actual = read(root, record->artifact);
  const auto metadata = read(root, record->metadata);
  if (cooked.isErr() || !actual || !metadata || cooked.value().metadataText != *metadata)
    return R::Err(E::InvalidGroup);
  const auto cookedBytes = std::as_bytes(std::span(cooked.value().artifactBytes));
  if (!std::ranges::equal(cookedBytes, std::as_bytes(std::span(*actual)))) return R::Err(E::InvalidGroup);

  const auto& nodes = parsed.value().nodes;
  std::map<std::uint32_t, std::size_t> indices;
  for (std::size_t index = 0; index < nodes.size(); ++index) indices.emplace(nodes[index].nodeId, index);
  ac::StaticGroup result{guid, *name, {}};
  for (const auto& node : nodes) {
    const auto& t = node.transform;
    ac::StaticGroupNode item{node.parentNodeId ? std::optional{indices.at(*node.parentNodeId)} : std::nullopt,
      {t.positionX, t.positionY, t.positionZ}, {t.eulerXRadians, t.eulerYRadians, t.eulerZRadians},
      {t.scaleX, t.scaleY, t.scaleZ}, std::nullopt, node.name};
    if (node.meshAsset) item.renderable = ac::StaticGroupRenderable{*node.meshAsset, node.materialAsset};
    result.nodes.push_back(std::move(item));
  }
  return R::Ok(std::move(result));
}
}  // namespace atlantis::runtime::prefabs
