#include <atlantis/asset_system/asset_catalog_source.h>

#include <atlantis/asset_system/logical_path.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <set>
#include <utility>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_asset_catalog_source_version: 1";
constexpr std::string_view kEntryCountPrefix = "entry_count: ";
constexpr std::string_view kEntryPrefix = "asset: ";
constexpr std::array<std::string_view, 4> kFieldPrefixes = {"guid=", "type=", "root=", "path="};

constexpr std::array<std::pair<CatalogAssetType, std::string_view>, 6> kTypeTokens = {{
    {CatalogAssetType::Mesh, "mesh"},
    {CatalogAssetType::Texture, "texture"},
    {CatalogAssetType::Material, "material"},
    {CatalogAssetType::Scene, "scene"},
    {CatalogAssetType::Environment, "environment"},
    {CatalogAssetType::GltfImport, "gltf_import"},
}};

constexpr std::array<std::pair<CatalogRoot, std::string_view>, 2> kRootTokens = {{
    {CatalogRoot::Assets, "assets"},
    {CatalogRoot::Content, "content"},
}};

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

// Catalog order: bytewise on the root token, then on the path.
[[nodiscard]] bool keyLess(CatalogRoot lhsRoot, std::string_view lhsPath, CatalogRoot rhsRoot,
                           std::string_view rhsPath) noexcept {
  const std::string_view lhs = toString(lhsRoot);
  const std::string_view rhs = toString(rhsRoot);
  if (lhs != rhs) return lhs < rhs;
  return lhsPath < rhsPath;
}

[[nodiscard]] bool entryLess(const CatalogSourceEntry& lhs, const CatalogSourceEntry& rhs) noexcept {
  return keyLess(lhs.root, lhs.path, rhs.root, rhs.path);
}

// Splits "asset: guid=… type=… root=… path=…" into its four values, in
// that exact order, separated by single spaces.
[[nodiscard]] bool splitEntryFields(std::string_view line, std::array<std::string_view, 4>& values) {
  if (line.substr(0, kEntryPrefix.size()) != kEntryPrefix) return false;
  std::string_view rest = line.substr(kEntryPrefix.size());
  for (std::size_t field = 0; field < kFieldPrefixes.size(); ++field) {
    const std::string_view prefix = kFieldPrefixes[field];
    if (rest.substr(0, prefix.size()) != prefix) return false;
    rest.remove_prefix(prefix.size());
    const bool last = field + 1 == kFieldPrefixes.size();
    const std::size_t end = last ? rest.size() : rest.find(' ');
    if (end == std::string_view::npos || end == 0) return false;
    values[field] = rest.substr(0, end);
    if (last) return values[field].find(' ') == std::string_view::npos;
    rest.remove_prefix(end + 1);
  }
  return false;
}

}  // namespace

std::string_view toString(CatalogAssetType type) noexcept {
  for (const auto& [value, token] : kTypeTokens) {
    if (value == type) return token;
  }
  return {};
}

std::string_view toString(CatalogRoot root) noexcept {
  for (const auto& [value, token] : kRootTokens) {
    if (value == root) return token;
  }
  return {};
}

std::optional<CatalogAssetType> parseCatalogAssetType(std::string_view token) noexcept {
  for (const auto& [value, text] : kTypeTokens) {
    if (text == token) return value;
  }
  return std::nullopt;
}

std::optional<CatalogRoot> parseCatalogRoot(std::string_view token) noexcept {
  for (const auto& [value, text] : kRootTokens) {
    if (text == token) return value;
  }
  return std::nullopt;
}

const CatalogSourceEntry* AssetCatalogSource::find(CatalogRoot root, std::string_view path) const noexcept {
  const auto it = std::lower_bound(entries_.begin(), entries_.end(), std::pair{root, path},
                                   [](const CatalogSourceEntry& entry, const auto& key) {
                                     return keyLess(entry.root, entry.path, key.first, key.second);
                                   });
  if (it == entries_.end() || it->root != root || it->path != path) return nullptr;
  return &*it;
}

const CatalogSourceEntry* AssetCatalogSource::find(const AssetGuid& guid) const noexcept {
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&guid](const CatalogSourceEntry& entry) { return entry.guid == guid; });
  return it == entries_.end() ? nullptr : &*it;
}

atlantis::Result<AssetCatalogSource, CatalogSourceParseError> parseAssetCatalogSource(std::string_view text) {
  using ResultT = atlantis::Result<AssetCatalogSource, CatalogSourceParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.empty() || lines[0] != kVersionLine) return ResultT::Err(CatalogSourceParseError::UnknownVersion);

  std::size_t declaredCount = 0;
  if (lines.size() < 2 || lines[1].substr(0, kEntryCountPrefix.size()) != kEntryCountPrefix) {
    return ResultT::Err(CatalogSourceParseError::EntryCountMismatch);
  }
  const std::string_view countText = lines[1].substr(kEntryCountPrefix.size());
  const auto [ptr, ec] = std::from_chars(countText.data(), countText.data() + countText.size(), declaredCount);
  if (countText.empty() || ec != std::errc{} || ptr != countText.data() + countText.size() ||
      declaredCount != lines.size() - 2) {
    return ResultT::Err(CatalogSourceParseError::EntryCountMismatch);
  }

  std::vector<CatalogSourceEntry> entries;
  entries.reserve(declaredCount);
  std::set<AssetGuid> seenGuids;
  for (std::size_t i = 2; i < lines.size(); ++i) {
    std::array<std::string_view, 4> values{};
    if (!splitEntryFields(lines[i], values)) return ResultT::Err(CatalogSourceParseError::MalformedEntry);

    const auto guid = parseAssetGuid(values[0]);
    if (guid.isErr()) {
      return ResultT::Err(guid.error() == GuidParseError::NilGuid ? CatalogSourceParseError::NilGuid
                                                                   : CatalogSourceParseError::MalformedEntry);
    }
    const auto type = parseCatalogAssetType(values[1]);
    if (!type) return ResultT::Err(CatalogSourceParseError::UnknownType);
    const auto root = parseCatalogRoot(values[2]);
    if (!root) return ResultT::Err(CatalogSourceParseError::UnknownRoot);
    const auto normalized = normalizeLogicalPath(values[3]);
    if (normalized.isErr() || normalized.value() != values[3]) {
      return ResultT::Err(CatalogSourceParseError::NonNormalPath);
    }

    CatalogSourceEntry entry{guid.value(), *type, *root, std::string(values[3])};
    if (!entries.empty()) {
      const CatalogSourceEntry& previous = entries.back();
      if (previous.root == entry.root && previous.path == entry.path) {
        return ResultT::Err(CatalogSourceParseError::DuplicatePath);
      }
      if (!entryLess(previous, entry)) return ResultT::Err(CatalogSourceParseError::Unsorted);
    }
    if (!seenGuids.insert(entry.guid).second) return ResultT::Err(CatalogSourceParseError::DuplicateGuid);
    entries.push_back(std::move(entry));
  }
  return ResultT::Ok(AssetCatalogSource(std::move(entries)));
}

std::string formatCatalogSourceEntry(const CatalogSourceEntry& entry) {
  std::string line(kEntryPrefix);
  line += "guid=";
  line += toString(entry.guid);
  line += " type=";
  line += toString(entry.type);
  line += " root=";
  line += toString(entry.root);
  line += " path=";
  line += entry.path;
  return line;
}

std::string serializeAssetCatalogSource(std::vector<CatalogSourceEntry> entries) {
  std::stable_sort(entries.begin(), entries.end(), entryLess);
  std::string text(kVersionLine);
  text += "\n";
  text += kEntryCountPrefix;
  text += std::to_string(entries.size());
  text += "\n";
  for (const CatalogSourceEntry& entry : entries) {
    text += formatCatalogSourceEntry(entry);
    text += "\n";
  }
  return text;
}

}  // namespace atlantis::asset_system
