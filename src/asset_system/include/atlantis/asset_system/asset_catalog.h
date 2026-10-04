#pragma once

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace atlantis::asset_system {

// Plan 0047 P12 / ADR-0098 D2: the cooked catalog, a build output assembled
// from per-cook fragments. A fragment shares the grammar but carries
// absolute locations; the assembled catalog's are relative to its own
// directory.

// A record's `source`: <root>:<path>, plus #<sub-key> for an imported
// sub-asset (P5). subKey is empty for a root asset.
struct CatalogSourceId {
  CatalogRoot root = CatalogRoot::Assets;
  std::string path;  // normalized logical path
  std::string subKey;
};

// nullopt unless the root token is known, the path is non-empty and already
// normalized, and a present sub-key is non-empty printable ASCII without
// spaces.
[[nodiscard]] std::optional<CatalogSourceId> parseCatalogSourceId(std::string_view text);
[[nodiscard]] std::string toString(const CatalogSourceId& id);

struct AssetCatalogRecord {
  AssetGuid guid;
  AssetId assetId = 0;
  CatalogAssetType type = CatalogAssetType::Mesh;  // never GltfImport
  CatalogSourceId source;
  std::string artifact;
  std::string metadata;
  std::uint32_t artifactSchema = 0;
  std::optional<std::uint32_t> sourceSchema;  // `none` when absent
  std::string tool;
  std::vector<AssetGuid> dependencies;
};

enum class AssetCatalogParseError {
  UnknownVersion,
  RecordCountMismatch,
  MalformedRecord,
  // Records out of GUID order, or a record's deps not strictly ascending.
  Unsorted,
};

// One record as its `record:` line, without a terminator.
[[nodiscard]] std::string formatAssetCatalogRecord(const AssetCatalogRecord& record);

// The full text (LF endings): records sorted by GUID, each record's
// dependencies sorted and de-duplicated. Does not validate the records.
[[nodiscard]] std::string serializeAssetCatalog(std::vector<AssetCatalogRecord> records);

// Accepts LF or CRLF and an optional final newline. Equal adjacent GUIDs
// parse, so that a duplicate surfaces as an assembly error naming it.
// Locations are taken verbatim; whether they must be absolute or relative
// is the caller's rule.
[[nodiscard]] atlantis::Result<std::vector<AssetCatalogRecord>, AssetCatalogParseError> parseAssetCatalogRecords(
    std::string_view text);

// One line of <build>/assets/declarations.txt (Plan 0047 P10).
struct AssetDeclaration {
  CatalogAssetType type = CatalogAssetType::Mesh;
  CatalogRoot root = CatalogRoot::Assets;
  std::string path;
};

// "type<TAB>root<TAB>path" per line; blank lines are skipped. nullopt on the
// first malformed line.
[[nodiscard]] std::optional<std::vector<AssetDeclaration>> parseAssetDeclarations(std::string_view text);

// Plan 0047 P13: every way assembly fails. Each names its subject.
enum class AssetCatalogAssemblyError {
  FragmentUnreadable,
  MalformedFragment,
  DuplicateGuid,
  DuplicateAssetId,
  ZeroAssetId,
  AssetIdMismatch,
  DanglingDependency,
  DependencyTypeMismatch,
  DeclarationNotInCatalog,
  DeclarationTypeMismatch,
  SidecarGuidMismatch,
  LocationEscapesCatalog,
  UnknownClosureScene,
};

[[nodiscard]] std::string_view toString(AssetCatalogAssemblyError error) noexcept;

struct AssetCatalogAssemblyFailure {
  AssetCatalogAssemblyError error = AssetCatalogAssemblyError::FragmentUnreadable;
  // The fragment path, record GUID or declaration the error is about.
  std::string subject;
};

struct AssetCatalogClosureRequest {
  AssetGuid scene;
  std::string outPath;
};

struct AssetCatalogAssemblyRequest {
  const AssetCatalogSource* catalogSource = nullptr;
  std::vector<AssetDeclaration> declarations;
  std::vector<std::string> fragmentPaths;
  std::string outPath;
  std::vector<AssetCatalogClosureRequest> closures;
};

struct AssembledAssetCatalog {
  std::string catalogText;
  std::vector<std::string> closureTexts;  // parallel to the request's closures
  std::size_t recordCount = 0;
  // Catalog-source entries no declaration of this build names
  // (content-gated): counted, never an error.
  std::size_t undeclaredSourceEntries = 0;
};

// Reads every fragment and each record's metadata sidecar, checks the whole
// set, and returns the catalog and closure texts for the caller to write.
// A closure is the scene's record plus its transitive dependencies, located
// relative to the closure file's own directory.
[[nodiscard]] atlantis::Result<AssembledAssetCatalog, AssetCatalogAssemblyFailure> assembleAssetCatalog(
    const AssetCatalogAssemblyRequest& request);

// Plan 0047 P14 / ADR-0098 D3: the Runtime's read side of an assembled (or
// closure) catalog.
enum class AssetCatalogError {
  Unreadable,
  Unparseable,
  // A record's artifact or metadata location is absolute or leaves the
  // catalog's directory.
  LocationNotRelative,
  DuplicateGuid,
  DuplicateAssetId,
  ZeroAssetId,
  AssetIdMismatch,
};

[[nodiscard]] std::string_view toString(AssetCatalogError error) noexcept;

// Immutable once built; safe for concurrent reads. Records are held sorted by
// AssetId; their artifact and metadata locations are already resolved against
// the catalog's own directory ('/'-separated).
class AssetCatalog {
 public:
  [[nodiscard]] const AssetCatalogRecord* find(AssetId id) const noexcept;
  [[nodiscard]] const AssetCatalogRecord* find(const AssetGuid& guid) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
  [[nodiscard]] std::span<const AssetCatalogRecord> records() const noexcept { return records_; }

 private:
  friend atlantis::Result<AssetCatalog, AssetCatalogError> parseAssetCatalog(std::string_view text,
                                                                              const std::string& directory);
  std::vector<AssetCatalogRecord> records_;     // sorted by assetId
  std::vector<std::size_t> guidOrder_;          // indices into records_, sorted by guid
};

// Parses the catalog text and validates it (each key is assetKey(guid), none
// zero, none shared; no duplicate GUID; locations relative and contained);
// `directory` is the catalog file's own directory.
[[nodiscard]] atlantis::Result<AssetCatalog, AssetCatalogError> parseAssetCatalog(std::string_view text,
                                                                                  const std::string& directory);

// Reads and parses the catalog file at `path`.
[[nodiscard]] atlantis::Result<AssetCatalog, AssetCatalogError> loadAssetCatalog(const std::string& path);

namespace detail {

// The key checks, separable so tests can inject keys: assembly first proves
// each asset_id == assetKey(guid), so a real zero or colliding key is only
// reachable through a genuine FNV-1a-64 outcome.
struct AssetKeyEntry {
  AssetId assetId = 0;
  std::string subject;
};

// ZeroAssetId for the first zero key, then DuplicateAssetId for the first
// shared key.
[[nodiscard]] atlantis::Result<std::monostate, AssetCatalogAssemblyFailure> checkAssetKeys(
    std::span<const AssetKeyEntry> entries);

}  // namespace detail

}  // namespace atlantis::asset_system
