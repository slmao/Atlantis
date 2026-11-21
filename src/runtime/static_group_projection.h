#pragma once

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/connection/asset_connection.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <set>

namespace atlantis::runtime::static_groups {
namespace as = asset_system;
namespace ac = connection::assets;
namespace fs = std::filesystem;

#ifdef ATLANTIS_REBUILD_TEST_CMAKE
inline void (*afterImportPreparation)(const fs::path&) = nullptr;
#endif

inline fs::path pathFromUtf8(std::string_view value) {
  return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

inline std::optional<std::string> managedName(const as::AssetCatalogRecord& record) {
  constexpr std::string_view prefix = "managed_";
  constexpr std::string_view suffix = "/source.gltf";
  const std::string_view source = record.source.path;
  if (record.type != as::CatalogAssetType::Scene || record.source.root != as::CatalogRoot::Content ||
      record.source.subKey != "scene" || record.tool != as::kImporterVersion ||
      source.size() <= prefix.size() + suffix.size() || !source.starts_with(prefix) ||
      !source.ends_with(suffix)) return std::nullopt;
  const auto name = source.substr(prefix.size(), source.size() - prefix.size() - suffix.size());
  return ac::validImportName(name) ? std::optional<std::string>(name) : std::nullopt;
}

inline bool ordinaryPath(const fs::path& root, const fs::path& file) {
  const auto relative = file.lexically_relative(root);
  if (relative.empty() || relative.has_root_path()) return false;
  auto path = root;
  for (auto part = relative.begin(); part != relative.end(); ++part) {
    if (part->empty() || *part == "." || *part == ".." ||
        part->native().find(fs::path::value_type(':')) != fs::path::string_type::npos) return false;
    path /= *part;
    const bool last = std::next(part) == relative.end();
#ifdef _WIN32
    const auto& leaf = part->native();
    if (leaf.back() == L'.' || leaf.back() == L' ') return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        last == ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) return false;
#else
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec || (last ? !fs::is_regular_file(status) : !fs::is_directory(status))) return false;
#endif
  }
  return true;
}

inline bool readBounded(const fs::path& root, const std::string& source, char* bytes,
                        std::size_t& remaining, std::size_t& size) {
  const auto path = pathFromUtf8(source);
  if (!ordinaryPath(root, path)) return false;
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  input.read(bytes, static_cast<std::streamsize>(remaining));
  size = static_cast<std::size_t>(input.gcount());
  if (input.bad() || input.peek() != std::char_traits<char>::eof() || !size) return false;
  remaining -= size;
  return true;
}

inline ac::StaticGroupResult project(const fs::path& root, const as::AssetCatalog& catalog, const as::AssetGuid& guid) {
  using R = ac::StaticGroupResult;
  using E = ac::StaticGroupError;
  const auto* record = catalog.find(guid);
  if (!record) return R::Err(E::UnknownAsset);
  auto name = managedName(*record);
  if (!name) return R::Err(E::UnsupportedGroup);

  as::CookedScene bytes;
  bytes.artifactBytes.resize(ac::kStaticGroupByteLimit);
  std::size_t remaining = bytes.artifactBytes.size();
  std::size_t size = 0;
  if (!readBounded(root, record->artifact, reinterpret_cast<char*>(bytes.artifactBytes.data()), remaining, size))
    return R::Err(E::InvalidGroup);
  bytes.artifactBytes.resize(size);
  bytes.metadataText.resize(remaining);
  if (!readBounded(root, record->metadata, bytes.metadataText.data(), remaining, size))
    return R::Err(E::InvalidGroup);
  bytes.metadataText.resize(size);
  const auto metadata = as::parseSceneMetadata(bytes.metadataText);
  if (metadata.isErr() || metadata.value().assetGuid != guid ||
      metadata.value().schemaVersion != record->artifactSchema) return R::Err(E::InvalidGroup);
  auto decoded = as::decodeScene(bytes);
  if (decoded.isErr()) return R::Err(E::InvalidGroup);
  const auto& scene = decoded.value();
  if (scene.activeCameraIndex() || scene.nodeCount() > ac::kStaticGroupNodeLimit) return R::Err(E::InvalidGroup);
  std::size_t renderables = 0;
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (scene.node(i).renderable) ++renderables;
  }
  // The original 0086 registered one-mesh shape remains readable by list/get.
  if (renderables == 1 && scene.nodeCount() == 1) return R::Err(E::UnsupportedGroup);
  if (renderables < 2) return R::Err(E::InvalidGroup);

  ac::StaticGroup group{guid, std::move(*name), {}};
  std::set<as::AssetGuid> dependencies;
  const auto resolve = [&](as::AssetId id, as::CatalogAssetType type) -> std::optional<as::AssetGuid> {
    const auto* dependency = catalog.find(id);
    if (!dependency || dependency->type != type || dependency->source.root != record->source.root ||
        dependency->source.path != record->source.path) return std::nullopt;
    dependencies.insert(dependency->guid);
    return dependency->guid;
  };
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    const auto& source = scene.node(i);
    const auto& t = source.transform;
    if (source.camera || source.light || t.scaleX == 0 || t.scaleY == 0 || t.scaleZ == 0)
      return R::Err(E::InvalidGroup);
    ac::StaticGroupNode node{scene.parentOf(i), {t.positionX, t.positionY, t.positionZ},
        {t.eulerXRadians, t.eulerYRadians, t.eulerZRadians}, {t.scaleX, t.scaleY, t.scaleZ}, {}};
    if (source.renderable) {
      const auto mesh = resolve(source.renderable->meshAsset, as::CatalogAssetType::Mesh);
      if (!mesh) return R::Err(E::InvalidGroup);
      std::optional<as::AssetGuid> material;
      if (source.renderable->materialAsset) {
        material = resolve(*source.renderable->materialAsset, as::CatalogAssetType::Material);
        if (!material) return R::Err(E::InvalidGroup);
      }
      node.renderable = ac::StaticGroupRenderable{*mesh, material};
    }
    group.nodes.push_back(node);
  }
  if (!std::ranges::equal(record->dependencies, dependencies)) return R::Err(E::InvalidGroup);
  return R::Ok(std::move(group));
}
}  // namespace atlantis::runtime::static_groups
