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
#include <array>
#include <cmath>
#include <iterator>
#include <map>
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

namespace json = connection::json;
using Guid = as::AssetGuid;
using State = ac::StaticGroupReviewState;
using Change = ac::StaticGroupChange;

struct SourceProduct {
  ac::StaticGroupRevisionRow display;
  std::optional<std::array<float, 6>> recipe;
};
using SourceProducts = std::map<std::string, SourceProduct, std::less<>>;

inline std::size_t members(const json::Value& value, std::string_view key) {
  return value.isObject() ? static_cast<std::size_t>(std::ranges::count_if(value.asObject(),
      [key](const auto& member) { return member.first == key; })) : 0;
}

inline std::optional<bool> keyedMode(const json::Value& source) {
  if (!source.isObject() || members(source, "asset") != 1) return {};
  const auto* asset = source.find("asset");
  if (!asset->isObject() || members(*asset, "extras") > 1) return {};
  const auto* extras = asset->find("extras");
  if (!extras || !extras->isObject() || !members(*extras, "atlantisReimportIdentity")) return false;
  const auto* mode = extras->find("atlantisReimportIdentity");
  if (members(*extras, "atlantisReimportIdentity") != 1 || !mode->isNumber() || mode->numberText() != "1") return {};
  return true;
}

inline std::optional<std::string> stableId(const json::Value& item) {
  if (!item.isObject() || members(item, "extras") != 1) return {};
  const auto* extras = item.find("extras");
  if (members(*extras, "atlantisId") != 1) return {};
  const auto* id = extras->find("atlantisId");
  if (!id->isString()) return {};
  const auto& text = id->asString();
  if (text.empty() || text.size() > 32 || text.front() < 'a' || text.front() > 'z' ||
      !std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      })) return {};
  return text;
}

inline std::string label(const json::Value& item) {
  const auto* name = item.find("name");
  if (!name || !name->isString() || name->asString().size() > 128 ||
      std::ranges::any_of(name->asString(), [](unsigned char ch) { return ch < 32 || ch == 127; })) return {};
  return name->asString();
}

inline std::optional<std::array<float, 6>> recipe(const json::Value& material) {
  std::array<float, 6> out{1, 1, 1, 1, 1, 1};
  const auto* pbr = material.find("pbrMetallicRoughness");
  if (!pbr) return out;
  if (!pbr->isObject() || members(material, "pbrMetallicRoughness") != 1) return {};
  const auto factor = [](const json::Value& value, float& part) {
    return value.toFloat(part) && std::isfinite(part) && part >= 0 && part <= 1;
  };
  if (const auto* color = pbr->find("baseColorFactor")) {
    if (members(*pbr, "baseColorFactor") != 1 || !color->isArray() || color->asArray().size() != 4) return {};
    for (std::size_t index = 0; index < 4; ++index)
      if (!factor(color->asArray()[index], out[index])) return {};
  }
  const auto* metal = pbr->find("metallicFactor");
  const auto* rough = pbr->find("roughnessFactor");
  if (out[3] != 1 || (metal && (members(*pbr, "metallicFactor") != 1 || !factor(*metal, out[4]))) ||
      (rough && (members(*pbr, "roughnessFactor") != 1 || !factor(*rough, out[5])))) return {};
  return out;
}

// Structural review. The managed importer still owns all deep geometry validation.
inline Result<SourceProducts, std::string> sourceProducts(const json::Value& source, const Guid& root) {
  using R = Result<SourceProducts, std::string>;
  if (keyedMode(source) != true) return R::Err("The source needs its original stable keys and opt-in marker.");
  const auto* meshes = source.find("meshes");
  const auto* materials = source.find("materials");
  const auto bounded = [](const json::Value* array) {
    return array && array->isArray() && array->asArray().size() <= ac::kStaticGroupNodeLimit;
  };
  if (members(source, "meshes") != 1 || members(source, "materials") > 1 || !bounded(meshes) ||
      (materials && !bounded(materials))) return R::Err("Malformed or oversized mesh or material table.");
  SourceProducts products;
  std::vector<Guid> materialGuids;
  if (materials) for (const auto& material : materials->asArray()) {
    const auto id = stableId(material);
    if (!id) return R::Err("Missing or invalid stable material ID at array position " + std::to_string(materialGuids.size()) + ".");
    auto key = "material-key/" + *id;
    const auto values = recipe(material);
    if (!values) return R::Err("Invalid opaque PBR recipe for " + key + ".");
    ac::StaticGroupRevisionRow row{label(material), static_cast<std::uint32_t>(materialGuids.size()), {}, {}};
    if (!products.emplace(key, SourceProduct{std::move(row), values}).second)
      return R::Err("Ambiguous duplicate stable key " + key + ".");
    materialGuids.push_back(as::deriveAssetGuid(root, key));
  }
  std::set<std::string> meshIds;
  std::size_t slots = 0, m = 0;
  for (const auto& mesh : meshes->asArray()) {
    const auto id = stableId(mesh);
    if (!id) return R::Err("Missing or invalid stable mesh ID at array position " + std::to_string(m) + ".");
    if (!meshIds.insert(*id).second) return R::Err("Ambiguous duplicate stable mesh ID " + *id + ".");
    const auto* parts = mesh.find("primitives");
    if (members(mesh, "primitives") != 1 || !bounded(parts) || parts->asArray().empty() ||
        parts->asArray().size() > ac::kStaticGroupNodeLimit - slots)
      return R::Err("Malformed or oversized slot table for " + *id + ".");
    slots += parts->asArray().size();
    std::size_t p = 0;
    for (const auto& part : parts->asArray()) {
      const auto slotId = stableId(part);
      if (!slotId) return R::Err("Missing or invalid stable slot ID on " + *id + " at position " + std::to_string(p) + ".");
      auto key = "mesh-key/" + *id + "/" + *slotId;
      ac::StaticGroupRevisionRow row{label(mesh), static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(p), {}};
      if (const auto* value = part.find("material")) {
        std::uint64_t index = 0;
        if (members(part, "material") != 1 || !value->toUInt64(index) || index >= materialGuids.size())
          return R::Err("Missing or invalid material assignment for " + key + ".");
        row.material = materialGuids[static_cast<std::size_t>(index)];
      }
      if (!products.emplace(key, SourceProduct{std::move(row), {}}).second)
        return R::Err("Ambiguous duplicate stable key " + key + ".");
      ++p;
    }
    ++m;
  }
  return slots ? R::Ok(std::move(products)) : R::Err("The source has no reviewable submesh slots.");
}

inline void compareProducts(ac::StaticGroupReview& review, const SourceProducts& before,
                            const SourceProducts& after, const Guid& root) {
  review.state = State::Ready;
  for (const auto& [key, published] : before) {
    const auto candidate = after.find(key);
    const bool missing = candidate == after.end();
    const bool assignment = !missing && published.display.material != candidate->second.display.material;
    const bool newRecipe = !missing && published.recipe != candidate->second.recipe;
    const auto change = missing ? Change::Removed : assignment || newRecipe ? Change::Conflict : Change::Matched;
    if (change != Change::Matched) review.state = State::Blocked;
    review.rows.push_back({key, as::deriveAssetGuid(root, key), change, published.display,
        missing ? std::nullopt : std::optional{candidate->second.display},
        missing ? "Missing from the candidate" : assignment ? "Existing material assignment changed" :
        newRecipe ? "Existing material recipe changed" : ""});
  }
  for (const auto& [key, candidate] : after)
    if (!before.contains(key)) review.rows.push_back({key, as::deriveAssetGuid(root, key), Change::Added, {}, candidate.display, {}});
  std::ranges::sort(review.rows, {}, &ac::StaticGroupChangeRow::key);
  if (review.state == State::Blocked) review.diagnostic = "Preserve each existing part, material and assignment before reimporting.";
}

inline bool matchesProducts(const fs::path& directory, const as::AssetCatalog& catalog,
                            const as::AssetCatalogRecord& record, const Guid& root, const SourceProducts& products) {
  const auto group = project(directory, catalog, record.guid);
  if (group.isErr()) return false;
  std::map<Guid, std::optional<Guid>> slots;
  const Guid white = as::deriveAssetGuid(root, "fallback/white");
  std::size_t materialCount = 0, count = 0;
  for (const auto& [key, product] : products) {
    const Guid guid = as::deriveAssetGuid(root, key);
    const auto* entry = catalog.find(guid);
    const auto type = product.recipe ? as::CatalogAssetType::Material : as::CatalogAssetType::Mesh;
    if (!entry || entry->source.root != record.source.root || entry->source.path != record.source.path ||
        entry->source.subKey != key || entry->type != type) return false;
    if (product.recipe) {
      if (entry->dependencies.size() != 1 || entry->dependencies.front() != white) return false;
      ++materialCount;
    } else {
      if (!entry->dependencies.empty()) return false;
      slots.emplace(guid, product.display.material);
    }
  }
  const auto* fallback = catalog.find(white);
  if (materialCount && (!fallback || fallback->type != as::CatalogAssetType::Texture ||
      fallback->source.path != record.source.path || fallback->source.root != record.source.root ||
      fallback->source.subKey != "fallback/white" || !fallback->dependencies.empty())) return false;
  for (const auto& entry : catalog.records())
    if (entry.source.root == record.source.root && entry.source.path == record.source.path) ++count;
  if (count != products.size() + 1 + (materialCount ? 1 : 0)) return false;
  std::set<Guid> visited;
  for (const auto& node : group.value().nodes) {
    if (!node.renderable) continue;
    const auto found = slots.find(node.renderable->mesh);
    if (found == slots.end() || found->second != node.renderable->material) return false;
    visited.insert(found->first);
  }
  return visited.size() == slots.size();
}
}  // namespace atlantis::runtime::static_groups
