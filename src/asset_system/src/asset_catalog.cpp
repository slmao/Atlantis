#include <atlantis/asset_system/asset_catalog.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/environment_metadata.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_metadata.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace atlantis::asset_system {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kVersionLine = "atlantis_asset_catalog_version: 1";
constexpr std::string_view kRecordCountPrefix = "record_count: ";
constexpr std::string_view kRecordPrefix = "record: ";

// The record's keys in their fixed order. A value runs up to the next key,
// so a location may contain spaces.
enum Field : std::size_t {
  kGuid,
  kAssetId,
  kType,
  kSource,
  kArtifact,
  kMetadata,
  kArtifactSchema,
  kSourceSchema,
  kTool,
  kDeps,
  kFieldCount,
};
constexpr std::array<std::string_view, kFieldCount> kFieldKeys = {
    "guid=",      " asset_id=",        " type=",          " source=", " artifact=",
    " metadata=", " artifact_schema=", " source_schema=", " tool=",   " deps=",
};

[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  if (text.empty()) return lines;
  std::string_view body = text;
  if (body.back() == '\n') body.remove_suffix(1);
  std::size_t start = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    if (i == body.size() || body[i] == '\n') {
      std::string_view line = body.substr(start, i - start);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      lines.push_back(line);
      start = i + 1;
    }
  }
  return lines;
}

[[nodiscard]] std::optional<std::uint32_t> parseU32(std::string_view text) {
  if (text.empty() || text.size() > 10) return std::nullopt;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
  }
  std::uint32_t value = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

[[nodiscard]] std::optional<AssetId> parseAssetIdHex(std::string_view text) {
  if (text.size() != 16) return std::nullopt;
  AssetId value = 0;
  for (const char c : text) {
    value <<= 4;
    if (c >= '0' && c <= '9') {
      value |= static_cast<AssetId>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= static_cast<AssetId>(c - 'a' + 10);
    } else {
      return std::nullopt;
    }
  }
  return value;
}

[[nodiscard]] bool isLocation(std::string_view text) {
  return !text.empty() && text.find_first_of("\t\r\n") == std::string_view::npos;
}

[[nodiscard]] bool isToken(std::string_view text) {
  if (text.empty()) return false;
  return std::all_of(text.begin(), text.end(), [](char c) { return c > ' ' && c < 0x7f; });
}

[[nodiscard]] std::optional<std::vector<AssetGuid>> parseDependencies(std::string_view text) {
  const std::size_t space = text.find(' ');
  const auto count = parseU32(text.substr(0, space));
  if (!count) return std::nullopt;
  std::vector<AssetGuid> dependencies;
  std::string_view rest = space == std::string_view::npos ? std::string_view{} : text.substr(space);
  for (std::uint32_t i = 0; i < *count; ++i) {
    if (rest.empty() || rest[0] != ' ') return std::nullopt;
    rest.remove_prefix(1);
    const std::size_t end = std::min(rest.find(' '), rest.size());
    const auto guid = parseAssetGuid(rest.substr(0, end));
    if (guid.isErr()) return std::nullopt;
    dependencies.push_back(guid.value());
    rest.remove_prefix(end);
  }
  if (!rest.empty()) return std::nullopt;
  return dependencies;
}

[[nodiscard]] std::optional<AssetCatalogRecord> parseRecordLine(std::string_view line) {
  if (line.substr(0, kRecordPrefix.size()) != kRecordPrefix) return std::nullopt;
  std::string_view rest = line.substr(kRecordPrefix.size());
  std::array<std::string_view, kFieldCount> values;
  for (std::size_t field = 0; field < kFieldCount; ++field) {
    const std::string_view key = kFieldKeys[field];
    if (rest.substr(0, key.size()) != key) return std::nullopt;
    rest.remove_prefix(key.size());
    const std::size_t end = field + 1 == kFieldCount ? rest.size() : rest.find(kFieldKeys[field + 1]);
    if (end == std::string_view::npos) return std::nullopt;
    values[field] = rest.substr(0, end);
    rest.remove_prefix(end);
  }

  AssetCatalogRecord record;
  const auto guid = parseAssetGuid(values[kGuid]);
  const auto assetId = parseAssetIdHex(values[kAssetId]);
  const auto type = parseCatalogAssetType(values[kType]);
  auto source = parseCatalogSourceId(values[kSource]);
  const auto artifactSchema = parseU32(values[kArtifactSchema]);
  auto dependencies = parseDependencies(values[kDeps]);
  if (guid.isErr() || !assetId || !type || *type == CatalogAssetType::GltfImport || !source || !artifactSchema ||
      !dependencies || !isLocation(values[kArtifact]) || !isLocation(values[kMetadata]) ||
      !isToken(values[kTool])) {
    return std::nullopt;
  }
  if (values[kSourceSchema] != "none") {
    const auto sourceSchema = parseU32(values[kSourceSchema]);
    if (!sourceSchema) return std::nullopt;
    record.sourceSchema = *sourceSchema;
  }
  record.guid = guid.value();
  record.assetId = *assetId;
  record.type = *type;
  record.source = std::move(*source);
  record.artifact = std::string(values[kArtifact]);
  record.metadata = std::string(values[kMetadata]);
  record.artifactSchema = *artifactSchema;
  record.tool = std::string(values[kTool]);
  record.dependencies = std::move(*dependencies);
  return record;
}

[[nodiscard]] bool dependencyAllowed(CatalogAssetType owner, CatalogAssetType dependency) noexcept {
  switch (owner) {
    case CatalogAssetType::Scene:
      return dependency == CatalogAssetType::Mesh || dependency == CatalogAssetType::Material;
    case CatalogAssetType::Material:
      return dependency == CatalogAssetType::Texture;
    case CatalogAssetType::Mesh:
    case CatalogAssetType::Texture:
    case CatalogAssetType::Environment:
    case CatalogAssetType::GltfImport:
      return false;
  }
  return false;
}

[[nodiscard]] std::optional<std::string> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad()) return std::nullopt;
  return text;
}

// The sidecar's own asset_guid, parsed by the record type's own metadata
// grammar; nullopt when the sidecar is unreadable or does not parse.
[[nodiscard]] std::optional<AssetGuid> readSidecarGuid(CatalogAssetType type, const std::string& path) {
  const auto text = readFile(path);
  if (!text) return std::nullopt;
  const auto guidOf = [](const auto& result) -> std::optional<AssetGuid> {
    if (result.isErr()) return std::nullopt;
    return result.value().assetGuid;
  };
  switch (type) {
    case CatalogAssetType::Mesh:
      return guidOf(parseAssetMetadata(*text));
    case CatalogAssetType::Texture:
      return guidOf(parseTextureMetadata(*text));
    case CatalogAssetType::Material:
      return guidOf(parseMaterialMetadata(*text));
    case CatalogAssetType::Scene:
      return guidOf(parseSceneMetadata(*text));
    case CatalogAssetType::Environment:
      return guidOf(parseEnvironmentMetadata(*text));
    case CatalogAssetType::GltfImport:
      return std::nullopt;
  }
  return std::nullopt;
}

// `location` relative to `directory`, '/'-separated; nullopt when it leaves
// the directory (a ".." step, another root, or the directory itself).
[[nodiscard]] std::optional<std::string> relativeLocation(const std::string& location, const fs::path& directory) {
  const fs::path relative = fs::path(location).lexically_normal().lexically_relative(directory);
  if (relative.empty() || relative == ".") return std::nullopt;
  for (const fs::path& part : relative) {
    if (part == "..") return std::nullopt;
  }
  return relative.generic_string();
}

[[nodiscard]] fs::path directoryOf(const std::string& outPath) {
  return fs::absolute(fs::path(outPath)).lexically_normal().parent_path();
}

using AssemblyResult = atlantis::Result<AssembledAssetCatalog, AssetCatalogAssemblyFailure>;

[[nodiscard]] AssemblyResult fail(AssetCatalogAssemblyError error, std::string subject) {
  return AssemblyResult::Err(AssetCatalogAssemblyFailure{error, std::move(subject)});
}

// The records with their absolute locations replaced by locations relative
// to `directory`; the failure names the first record that escapes it.
[[nodiscard]] atlantis::Result<std::vector<AssetCatalogRecord>, AssetCatalogAssemblyFailure> relocate(
    const std::vector<const AssetCatalogRecord*>& records, const fs::path& directory) {
  using ResultT = atlantis::Result<std::vector<AssetCatalogRecord>, AssetCatalogAssemblyFailure>;
  std::vector<AssetCatalogRecord> relocated;
  relocated.reserve(records.size());
  for (const AssetCatalogRecord* record : records) {
    auto artifact = relativeLocation(record->artifact, directory);
    auto metadata = relativeLocation(record->metadata, directory);
    if (!artifact || !metadata) {
      return ResultT::Err(
          AssetCatalogAssemblyFailure{AssetCatalogAssemblyError::LocationEscapesCatalog, toString(record->guid)});
    }
    AssetCatalogRecord copy = *record;
    copy.artifact = std::move(*artifact);
    copy.metadata = std::move(*metadata);
    relocated.push_back(std::move(copy));
  }
  return ResultT::Ok(std::move(relocated));
}

}  // namespace

std::optional<CatalogSourceId> parseCatalogSourceId(std::string_view text) {
  const std::size_t colon = text.find(':');
  if (colon == std::string_view::npos) return std::nullopt;
  const auto root = parseCatalogRoot(text.substr(0, colon));
  if (!root) return std::nullopt;
  std::string_view path = text.substr(colon + 1);
  std::string_view subKey;
  const std::size_t hash = path.find('#');
  if (hash != std::string_view::npos) {
    subKey = path.substr(hash + 1);
    path = path.substr(0, hash);
    if (!isToken(subKey)) return std::nullopt;
  }
  if (path.empty()) return std::nullopt;
  const auto normalized = normalizeLogicalPath(path);
  if (normalized.isErr() || normalized.value() != path) return std::nullopt;
  return CatalogSourceId{*root, std::string(path), std::string(subKey)};
}

std::string toString(const CatalogSourceId& id) {
  std::string text(toString(id.root));
  text += ':';
  text += id.path;
  if (!id.subKey.empty()) {
    text += '#';
    text += id.subKey;
  }
  return text;
}

std::string formatAssetCatalogRecord(const AssetCatalogRecord& record) {
  std::string line(kRecordPrefix);
  line += "guid=" + toString(record.guid);
  line += " asset_id=" + toHexString(record.assetId);
  line += " type=";
  line += toString(record.type);
  line += " source=" + toString(record.source);
  line += " artifact=" + record.artifact;
  line += " metadata=" + record.metadata;
  line += " artifact_schema=" + std::to_string(record.artifactSchema);
  line += " source_schema=" + (record.sourceSchema ? std::to_string(*record.sourceSchema) : std::string("none"));
  line += " tool=" + record.tool;
  line += " deps=" + std::to_string(record.dependencies.size());
  for (const AssetGuid& dependency : record.dependencies) line += " " + toString(dependency);
  return line;
}

std::string serializeAssetCatalog(std::vector<AssetCatalogRecord> records) {
  for (AssetCatalogRecord& record : records) {
    std::sort(record.dependencies.begin(), record.dependencies.end());
    record.dependencies.erase(std::unique(record.dependencies.begin(), record.dependencies.end()),
                              record.dependencies.end());
  }
  std::stable_sort(records.begin(), records.end(),
                   [](const AssetCatalogRecord& lhs, const AssetCatalogRecord& rhs) { return lhs.guid < rhs.guid; });
  std::string text(kVersionLine);
  text += "\n";
  text += kRecordCountPrefix;
  text += std::to_string(records.size()) + "\n";
  for (const AssetCatalogRecord& record : records) text += formatAssetCatalogRecord(record) + "\n";
  return text;
}

atlantis::Result<std::vector<AssetCatalogRecord>, AssetCatalogParseError> parseAssetCatalogRecords(
    std::string_view text) {
  using ResultT = atlantis::Result<std::vector<AssetCatalogRecord>, AssetCatalogParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.empty() || lines[0] != kVersionLine) return ResultT::Err(AssetCatalogParseError::UnknownVersion);
  if (lines.size() < 2 || lines[1].substr(0, kRecordCountPrefix.size()) != kRecordCountPrefix) {
    return ResultT::Err(AssetCatalogParseError::RecordCountMismatch);
  }
  const auto count = parseU32(lines[1].substr(kRecordCountPrefix.size()));
  if (!count || *count != lines.size() - 2) return ResultT::Err(AssetCatalogParseError::RecordCountMismatch);

  std::vector<AssetCatalogRecord> records;
  records.reserve(*count);
  for (std::size_t i = 2; i < lines.size(); ++i) {
    auto record = parseRecordLine(lines[i]);
    if (!record) return ResultT::Err(AssetCatalogParseError::MalformedRecord);
    if (!records.empty() && record->guid < records.back().guid) return ResultT::Err(AssetCatalogParseError::Unsorted);
    for (std::size_t d = 1; d < record->dependencies.size(); ++d) {
      if (!(record->dependencies[d - 1] < record->dependencies[d])) {
        return ResultT::Err(AssetCatalogParseError::Unsorted);
      }
    }
    records.push_back(std::move(*record));
  }
  return ResultT::Ok(std::move(records));
}

std::optional<std::vector<AssetDeclaration>> parseAssetDeclarations(std::string_view text) {
  std::vector<AssetDeclaration> declarations;
  for (const std::string_view line : splitLines(text)) {
    if (line.empty()) continue;
    const std::size_t first = line.find('\t');
    const std::size_t second = first == std::string_view::npos ? first : line.find('\t', first + 1);
    if (second == std::string_view::npos || line.find('\t', second + 1) != std::string_view::npos) {
      return std::nullopt;
    }
    const auto type = parseCatalogAssetType(line.substr(0, first));
    const auto root = parseCatalogRoot(line.substr(first + 1, second - first - 1));
    const std::string_view path = line.substr(second + 1);
    if (!type || !root || path.empty()) return std::nullopt;
    declarations.push_back(AssetDeclaration{*type, *root, std::string(path)});
  }
  return declarations;
}

std::string_view toString(AssetCatalogAssemblyError error) noexcept {
  switch (error) {
    case AssetCatalogAssemblyError::FragmentUnreadable:
      return "FragmentUnreadable";
    case AssetCatalogAssemblyError::MalformedFragment:
      return "MalformedFragment";
    case AssetCatalogAssemblyError::DuplicateGuid:
      return "DuplicateGuid";
    case AssetCatalogAssemblyError::DuplicateAssetId:
      return "DuplicateAssetId";
    case AssetCatalogAssemblyError::ZeroAssetId:
      return "ZeroAssetId";
    case AssetCatalogAssemblyError::AssetIdMismatch:
      return "AssetIdMismatch";
    case AssetCatalogAssemblyError::DanglingDependency:
      return "DanglingDependency";
    case AssetCatalogAssemblyError::DependencyTypeMismatch:
      return "DependencyTypeMismatch";
    case AssetCatalogAssemblyError::DeclarationNotInCatalog:
      return "DeclarationNotInCatalog";
    case AssetCatalogAssemblyError::DeclarationTypeMismatch:
      return "DeclarationTypeMismatch";
    case AssetCatalogAssemblyError::SidecarGuidMismatch:
      return "SidecarGuidMismatch";
    case AssetCatalogAssemblyError::LocationEscapesCatalog:
      return "LocationEscapesCatalog";
    case AssetCatalogAssemblyError::UnknownClosureScene:
      return "UnknownClosureScene";
  }
  return "(unrecognized AssetCatalogAssemblyError)";
}

namespace detail {

atlantis::Result<std::monostate, AssetCatalogAssemblyFailure> checkAssetKeys(std::span<const AssetKeyEntry> entries) {
  using ResultT = atlantis::Result<std::monostate, AssetCatalogAssemblyFailure>;
  for (const AssetKeyEntry& entry : entries) {
    if (entry.assetId == 0) {
      return ResultT::Err(AssetCatalogAssemblyFailure{AssetCatalogAssemblyError::ZeroAssetId, entry.subject});
    }
  }
  std::map<AssetId, const AssetKeyEntry*> seen;
  for (const AssetKeyEntry& entry : entries) {
    const auto [it, inserted] = seen.emplace(entry.assetId, &entry);
    if (!inserted) {
      return ResultT::Err(AssetCatalogAssemblyFailure{AssetCatalogAssemblyError::DuplicateAssetId,
                                                      it->second->subject + " and " + entry.subject});
    }
  }
  return ResultT::Ok(std::monostate{});
}

}  // namespace detail

AssemblyResult assembleAssetCatalog(const AssetCatalogAssemblyRequest& request) {
  ATLANTIS_CHECK_MSG(request.catalogSource != nullptr, "assembleAssetCatalog(): catalogSource is required");

  // Fragments: every location must be absolute.
  std::vector<AssetCatalogRecord> records;
  for (const std::string& fragmentPath : request.fragmentPaths) {
    const auto text = readFile(fragmentPath);
    if (!text) return fail(AssetCatalogAssemblyError::FragmentUnreadable, fragmentPath);
    auto parsed = parseAssetCatalogRecords(*text);
    if (parsed.isErr()) return fail(AssetCatalogAssemblyError::MalformedFragment, fragmentPath);
    for (AssetCatalogRecord& record : parsed.value()) {
      if (!fs::path(record.artifact).is_absolute() || !fs::path(record.metadata).is_absolute()) {
        return fail(AssetCatalogAssemblyError::MalformedFragment, fragmentPath);
      }
      records.push_back(std::move(record));
    }
  }
  std::stable_sort(records.begin(), records.end(),
                   [](const AssetCatalogRecord& lhs, const AssetCatalogRecord& rhs) { return lhs.guid < rhs.guid; });

  std::vector<const AssetCatalogRecord*> all;
  all.reserve(records.size());
  for (const AssetCatalogRecord& record : records) all.push_back(&record);
  auto relocated = relocate(all, directoryOf(request.outPath));
  if (relocated.isErr()) return AssemblyResult::Err(relocated.error());

  for (std::size_t i = 1; i < records.size(); ++i) {
    if (records[i].guid == records[i - 1].guid) {
      return fail(AssetCatalogAssemblyError::DuplicateGuid, toString(records[i].guid));
    }
  }
  std::vector<detail::AssetKeyEntry> keys;
  keys.reserve(records.size());
  for (const AssetCatalogRecord& record : records) {
    if (record.assetId != assetKey(record.guid)) {
      return fail(AssetCatalogAssemblyError::AssetIdMismatch, toString(record.guid));
    }
    keys.push_back(detail::AssetKeyEntry{record.assetId, toString(record.guid)});
  }
  if (const auto keyCheck = detail::checkAssetKeys(keys); keyCheck.isErr()) {
    return AssemblyResult::Err(keyCheck.error());
  }

  const auto findRecord = [&records](const AssetGuid& guid) -> const AssetCatalogRecord* {
    const auto it = std::lower_bound(records.begin(), records.end(), guid,
                                     [](const AssetCatalogRecord& record, const AssetGuid& key) {
                                       return record.guid < key;
                                     });
    return it != records.end() && it->guid == guid ? &*it : nullptr;
  };
  for (const AssetCatalogRecord& record : records) {
    for (const AssetGuid& dependency : record.dependencies) {
      const std::string subject = toString(record.guid) + " -> " + toString(dependency);
      const AssetCatalogRecord* target = findRecord(dependency);
      if (target == nullptr) return fail(AssetCatalogAssemblyError::DanglingDependency, subject);
      if (!dependencyAllowed(record.type, target->type)) {
        return fail(AssetCatalogAssemblyError::DependencyTypeMismatch, subject);
      }
    }
  }

  std::set<std::pair<CatalogRoot, std::string>> declared;
  for (const AssetDeclaration& declaration : request.declarations) {
    const std::string subject =
        std::string(toString(declaration.type)) + " " + std::string(toString(declaration.root)) + ":" + declaration.path;
    const CatalogSourceEntry* entry = request.catalogSource->find(declaration.root, declaration.path);
    if (entry == nullptr) return fail(AssetCatalogAssemblyError::DeclarationNotInCatalog, subject);
    if (entry->type != declaration.type) return fail(AssetCatalogAssemblyError::DeclarationTypeMismatch, subject);
    declared.emplace(declaration.root, declaration.path);
  }

  for (const AssetCatalogRecord& record : records) {
    const auto sidecarGuid = readSidecarGuid(record.type, record.metadata);
    if (!sidecarGuid || *sidecarGuid != record.guid) {
      return fail(AssetCatalogAssemblyError::SidecarGuidMismatch, toString(record.guid));
    }
  }

  AssembledAssetCatalog assembled;
  for (const AssetCatalogClosureRequest& closure : request.closures) {
    const AssetCatalogRecord* scene = findRecord(closure.scene);
    if (scene == nullptr || scene->type != CatalogAssetType::Scene) {
      return fail(AssetCatalogAssemblyError::UnknownClosureScene, toString(closure.scene));
    }
    std::set<AssetGuid> visited{scene->guid};
    std::deque<const AssetCatalogRecord*> pending{scene};
    std::vector<const AssetCatalogRecord*> members;
    while (!pending.empty()) {
      const AssetCatalogRecord* member = pending.front();
      pending.pop_front();
      members.push_back(member);
      for (const AssetGuid& dependency : member->dependencies) {
        if (visited.insert(dependency).second) pending.push_back(findRecord(dependency));
      }
    }
    auto closureRecords = relocate(members, directoryOf(closure.outPath));
    if (closureRecords.isErr()) return AssemblyResult::Err(closureRecords.error());
    assembled.closureTexts.push_back(serializeAssetCatalog(std::move(closureRecords.value())));
  }

  for (const CatalogSourceEntry& entry : request.catalogSource->entries()) {
    if (!declared.contains({entry.root, entry.path})) ++assembled.undeclaredSourceEntries;
  }
  assembled.recordCount = records.size();
  assembled.catalogText = serializeAssetCatalog(std::move(relocated.value()));
  return AssemblyResult::Ok(std::move(assembled));
}

}  // namespace atlantis::asset_system
