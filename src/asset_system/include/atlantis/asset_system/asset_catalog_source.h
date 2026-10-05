#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atlantis::asset_system {

// Plan 0047 P6 / ADR-0098 D1: the committed catalog source
// (assets/asset_catalog.txt), the only place a root GUID is written.

enum class CatalogAssetType { Mesh, Texture, Material, Scene, Environment, GltfImport };
enum class CatalogRoot { Assets, Content };

// Tokens as written in the catalog source: mesh, texture, material, scene,
// environment, gltf_import; assets, content.
[[nodiscard]] std::string_view toString(CatalogAssetType type) noexcept;
[[nodiscard]] std::string_view toString(CatalogRoot root) noexcept;
[[nodiscard]] std::optional<CatalogAssetType> parseCatalogAssetType(std::string_view token) noexcept;
[[nodiscard]] std::optional<CatalogRoot> parseCatalogRoot(std::string_view token) noexcept;

struct CatalogSourceEntry {
  AssetGuid guid;
  CatalogAssetType type = CatalogAssetType::Mesh;
  CatalogRoot root = CatalogRoot::Assets;
  std::string path;  // normalized logical path
};

// Reported for the first offending line. A missing or unparseable
// entry_count line reports EntryCountMismatch; a GUID that is malformed (as
// opposed to nil) reports MalformedEntry.
enum class CatalogSourceParseError {
  UnknownVersion,
  EntryCountMismatch,
  MalformedEntry,
  NilGuid,
  UnknownType,
  UnknownRoot,
  NonNormalPath,
  Unsorted,
  DuplicateGuid,
  DuplicatePath,
};

// A fully validated catalog source: entries are in bytewise (root token,
// path) order with unique GUIDs and unique (root, path) keys. Only
// parseAssetCatalogSource() constructs one. Immutable; safe for concurrent
// reads.
class AssetCatalogSource {
 public:
  AssetCatalogSource() = delete;

  [[nodiscard]] const std::vector<CatalogSourceEntry>& entries() const noexcept { return entries_; }
  // nullptr when absent. The returned pointer is valid for this object's
  // lifetime.
  [[nodiscard]] const CatalogSourceEntry* find(CatalogRoot root, std::string_view path) const noexcept;
  [[nodiscard]] const CatalogSourceEntry* find(const AssetGuid& guid) const noexcept;

 private:
  friend atlantis::Result<AssetCatalogSource, CatalogSourceParseError> parseAssetCatalogSource(std::string_view);

  explicit AssetCatalogSource(std::vector<CatalogSourceEntry> entries) : entries_(std::move(entries)) {}

  std::vector<CatalogSourceEntry> entries_;
};

// Accepts LF or CRLF line endings and an optional final newline.
[[nodiscard]] atlantis::Result<AssetCatalogSource, CatalogSourceParseError> parseAssetCatalogSource(
    std::string_view text);

// One entry as its `asset:` line, without a line terminator.
[[nodiscard]] std::string formatCatalogSourceEntry(const CatalogSourceEntry& entry);

// Writes the full text (LF endings) with entries sorted into catalog order.
// Does not validate: parseAssetCatalogSource() of the result reports any
// duplicate, nil GUID or non-normal path the input carried.
[[nodiscard]] std::string serializeAssetCatalogSource(std::vector<CatalogSourceEntry> entries);

}  // namespace atlantis::asset_system
