#include "asset_rebuild_host.h"
#include "material_authoring.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>

#include <atlantis/asset_system/guid_mint.h>
#include <atlantis/connection/json.h>
#include <atlantis/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::runtime {
namespace {
namespace fs = std::filesystem;
namespace as = asset_system;
namespace ca = connection::assets;
namespace json = connection::json;
using Clock = std::chrono::steady_clock;
using Stage = ca::RebuildStage;
using Code = ca::RebuildFailureCode;
constexpr std::string_view kCatalog = "asset_catalog.txt";
constexpr std::string_view kManifest = "generation.json";
constexpr std::string_view kDirty = ".atlantis-rebuild-dirty";
constexpr std::string_view kOwner = ".atlantis-rebuild-profile";
constexpr std::string_view kManagedProject = "assets/managed-project.txt";
constexpr std::string_view kPendingSelection = "assets/managed-pending.txt";
constexpr std::string_view kRemovalProfile = "assets/texture-removal-profile.txt";
constexpr std::string_view kManagedSource = "assets/managed-catalog-source.txt";
constexpr std::string_view kProjectDirty = ".atlantis-import-dirty";
constexpr std::string_view kMaterialSources = "assets/managed-material-sources";
constexpr std::string_view kMaterialLeaf = "source.material.txt";
constexpr std::string_view kTextureSources = "assets/managed-texture-sources";
constexpr std::string_view kTextureLocations = "texture-locations.json";
constexpr DWORD kLocationBytes = 2 * 1024 * 1024;
constexpr std::string_view kTextureNames = "texture-display-names.json";
constexpr DWORD kNameBytes = 512 * 1024;
constexpr std::size_t kNameLimit = 4096;
constexpr DWORD kPngBytes = 24 * 1024 * 1024;

class Handle {
 public:
  explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
  ~Handle() { if (*this) CloseHandle(value_); }
  Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  Handle& operator=(Handle&& other) noexcept {
    std::swap(value_, other.value_);
    return *this;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
  [[nodiscard]] HANDLE get() const noexcept { return value_; }
 private:
  HANDLE value_;
};

std::string utf8(const fs::path& path) {
  const auto chars = path.generic_u8string();
  return {chars.begin(), chars.end()};
}
fs::path fromUtf8(std::string_view text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}
std::optional<std::string> readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  return in.bad() ? std::nullopt : std::optional{std::move(contents)};
}
std::optional<json::Value> readJson(const fs::path& path) {
  const auto text = readText(path);
  if (!text) return std::nullopt;
  auto result = json::parse(*text);
  if (result.isErr() || !result.value().isObject()) return std::nullopt;
  return std::move(result.value());
}
bool writeText(const fs::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  out.close();
  return static_cast<bool>(out);
}
bool sameComponent(const fs::path& a, const fs::path& b) {
  return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool contains(const fs::path& root, const fs::path& path) {
  auto at = path.begin();
  for (const auto& part : root) {
    if (at == path.end() || !sameComponent(part, *at)) return false;
    ++at;
  }
  return true;
}
bool overlap(const fs::path& a, const fs::path& b) { return contains(a, b) || contains(b, a); }

// Validate every component, including a missing suffix for paths that have
// yet to be generated.
bool safeRelative(const fs::path& root, const fs::path& relative, bool allowMissing = false) {
  if (relative.empty() || relative.has_root_path()) return false;
  auto path = root;
  bool missing = false;
  for (auto i = relative.begin(); i != relative.end(); ++i) {
    const auto& name = i->native();
    if (name.empty() || name.back() == L'.' || name.back() == L' ' || name.find(L':') != std::wstring::npos)
      return false;
    if (missing) continue;
    path /= *i;
    const bool last = std::next(i) == relative.end();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      if (!allowMissing || GetLastError() != ERROR_FILE_NOT_FOUND) return false;
      missing = true;
      continue;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    if (last == ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) return false;
  }
  return true;
}
bool realDirectory(const fs::path& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
}
template<class Interrupted>
bool safeBuildTree(const fs::path& root, Interrupted interrupted) {
  std::error_code ec;
  for (fs::recursive_directory_iterator at(root, ec), end; !ec && at != end; at.increment(ec)) {
    if (interrupted()) return false;
    const DWORD attributes = GetFileAttributesW(at->path().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
  }
  return !ec;
}
std::optional<fs::path> canonicalDirectory(const json::Value* value) {
  if (!value || !value->isString()) return std::nullopt;
  const fs::path input = fromUtf8(value->asString());
  if (!input.is_absolute()) return std::nullopt;
  std::error_code ec;
  fs::path path = fs::canonical(input, ec);
  if (ec || !realDirectory(path)) return std::nullopt;
  return path;
}

bool safeProjectRoot(const fs::path& configured, const fs::path& canonical) {
  const auto normalized = configured.lexically_normal();
  if (!contains(normalized, canonical) || !contains(canonical, normalized)) return false;
  auto path = canonical;
  while (path.has_relative_path()) {
    if (!realDirectory(path)) return false;
    path = path.parent_path();
  }
  return true;
}

std::vector<std::string> columns(const std::string& line) {
  std::istringstream row(line);
  std::vector<std::string> out;
  for (std::string value; std::getline(row, value, '\t');) out.push_back(std::move(value));
  return out;
}
struct Producer {
  as::AssetGuid root;
  as::CatalogAssetType type;
  fs::path stamp;
  fs::path generated;
  friend bool operator==(const Producer&, const Producer&) = default;
};
using Producers = std::map<std::string, Producer, std::less<>>;
std::optional<Producers> readProducers(const std::string& data) {
  std::istringstream in(data);
  std::string line;
  if (!std::getline(in, line) || !line.starts_with("atlantis.asset-producers/1\t")) return std::nullopt;
  Producers out;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto row = columns(line);
    if (row.size() != 6) return std::nullopt;
    const auto source = as::parseCatalogSourceId(row[1] + ":" + row[2]);
    const auto guid = as::parseAssetGuid(row[3]);
    const auto type = as::parseCatalogAssetType(row[0]);
    if (!source || !source->subKey.empty() || guid.isErr() || !type ||
        (*type == as::CatalogAssetType::GltfImport) != (row[5] != "-")) return std::nullopt;
    const auto [ignored, inserted] = out.emplace(as::toString(*source),
        Producer{guid.value(), *type, fromUtf8(row[4]), row[5] == "-" ? fs::path{} : fromUtf8(row[5])});
    if (!inserted) return std::nullopt;
  }
  return out;
}

std::optional<as::AssetGuid> newGuid() {
  GUID native{};
  if (FAILED(CoCreateGuid(&native))) return std::nullopt;
  wchar_t wide[39]{};
  if (!StringFromGUID2(native, wide, 39)) return std::nullopt;
  std::string text;
  for (std::size_t i = 1; i <= 36; ++i)
    text.push_back(static_cast<char>(wide[i] >= L'A' && wide[i] <= L'F' ? wide[i] - L'A' + L'a' : wide[i]));
  auto result = as::parseAssetGuid(text);
  return result.isOk() ? std::optional{result.value()} : std::nullopt;
}
std::optional<as::AssetCatalog> readCatalog(const fs::path& root) {
  if (!safeRelative(root, fs::path(kCatalog))) return std::nullopt;
  auto text = readText(root / kCatalog);
  if (!text) return std::nullopt;
  auto parsed = as::parseAssetCatalog(*text, utf8(root));
  return parsed.isOk() ? std::optional{std::move(parsed.value())} : std::nullopt;
}
bool retainsIdentity(const as::AssetCatalog& a, const as::AssetCatalog& b) {
  for (const auto& record : a.records()) {
    const auto* other = b.find(record.guid);
    if (!other || record.type != other->type || as::toString(record.source) != as::toString(other->source)) return false;
  }
  return true;
}

using Registrations = std::map<std::string, as::AssetGuid, std::less<>>;

struct MaterialUnit {
  as::AssetGuid guid;
  std::string source;
  friend bool operator==(const MaterialUnit&, const MaterialUnit&) = default;
};
using Materials = std::map<std::string, MaterialUnit, std::less<>>;
std::string materialKey(std::string_view name) {
  return "content:material_" + std::string(name) + "/source.material.txt";
}
std::optional<std::string_view> materialName(std::string_view key) {
  constexpr std::string_view prefix = "content:material_";
  constexpr std::string_view suffix = "/source.material.txt";
  if (!key.starts_with(prefix) || !key.ends_with(suffix) || key.size() <= prefix.size() + suffix.size())
    return std::nullopt;
  const auto name = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
  return ca::validImportName(name) ? std::optional{name} : std::nullopt;
}
fs::path materialDirectory(std::string_view name) { return fromUtf8("material_" + std::string(name)); }
std::optional<std::string> readMaterialText(const fs::path& root, const fs::path& relative) {
  if (!safeRelative(root, relative)) return std::nullopt;
  std::ifstream in(root / relative, std::ios::binary);
  if (!in) return std::nullopt;
  std::string bytes(64 * 1024 + 1, '\0');
  in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!in.eof() || in.bad()) return std::nullopt;
  bytes.resize(static_cast<std::size_t>(in.gcount()));
  return bytes;
}
std::optional<Materials> materialInputs(const fs::path& root, const Producers& producers) {
  Materials result;
  for (const auto& [key, producer] : producers) {
    const auto name = materialName(key);
    if (!name) continue;
    const auto source = readMaterialText(root, fs::path(kMaterialSources) / materialDirectory(*name) / kMaterialLeaf);
    if (!source || producer.type != as::CatalogAssetType::Material ||
        as::parseMaterialSource(*source).isErr()) return std::nullopt;
    result.emplace(*name, MaterialUnit{producer.root, *source});
  }
  return result;
}
std::optional<Materials> registeredMaterials(const fs::path& root) {
  const auto registry = root / "registered";
  if (!realDirectory(registry)) return std::nullopt;
  Materials result;
  std::set<as::AssetGuid> identities;
  std::error_code ec;
  for (fs::directory_iterator at(registry, ec), end; !ec && at != end; at.increment(ec)) {
    const auto leaf = utf8(at->path().filename());
    if (!leaf.starts_with("material_")) continue;
    const auto name = std::string_view(leaf).substr(9);
    const auto source = readMaterialText(registry, fromUtf8(leaf) / kMaterialLeaf);
    const auto fragment = readMaterialText(registry, fromUtf8(leaf) / "catalog-source.txt");
    if (!ca::validImportName(name) || !source || !fragment) return std::nullopt;
    const auto parsed = as::parseAssetCatalogSource(*fragment);
    if (parsed.isErr() || parsed.value().entries().size() != 1) return std::nullopt;
    const auto& entry = parsed.value().entries().front();
    if (entry.root != as::CatalogRoot::Content || entry.type != as::CatalogAssetType::Material ||
        "content:" + entry.path != materialKey(name) || !identities.insert(entry.guid).second) return std::nullopt;
    result.emplace(name, MaterialUnit{entry.guid, *source});
  }
  return ec ? std::nullopt : std::optional{std::move(result)};
}

bool matchesFrozenMaterials(const as::AssetCatalog& catalog, const Materials& materials) {
  return std::ranges::all_of(materials, [&](const auto& row) {
    return material_authoring::matchesFrozen(catalog, row.second.guid, row.second.source);
  });
}

using TextureKind = AssetRebuildHost::TextureKind;
constexpr std::array kTextureLeaves{"source.png", "normal.png", "metallic-roughness.png", "occlusion.png"};
constexpr std::array kTextureRoles{"BaseColor", "Normal", "MetallicRoughness", "Occlusion"};
constexpr const char* textureLeaf(TextureKind kind) { return kTextureLeaves[static_cast<std::size_t>(kind)]; }
constexpr as::TextureColorSpace textureColor(TextureKind kind) {
  return kind == TextureKind::Color ? as::TextureColorSpace::Srgb : as::TextureColorSpace::Unorm;
}

struct TextureUnit {
  as::AssetGuid guid;
  std::string digest;
  TextureKind kind;
  friend bool operator==(const TextureUnit&, const TextureUnit&) = default;
};
using Textures = std::map<std::string, TextureUnit, std::less<>>;
using TextureNames = std::map<as::AssetGuid, std::string>;

json::Value nameValue(const TextureNames& names) {
  auto result = json::Value::object();
  for (const auto& [guid, label] : names) result.set(as::toString(guid), json::Value::string(label));
  return result;
}
std::optional<TextureNames> parseNames(const json::Value& value, const Textures& textures) {
  if (!value.isObject() || value.asObject().empty() || value.asObject().size() > kNameLimit)
    return std::nullopt;
  std::map<as::AssetGuid, std::string_view> colors;
  std::set<std::string_view> reserved;
  for (const auto& [token, unit] : textures) {
    if (unit.kind != TextureKind::Color) continue;
    colors.emplace(unit.guid, token);
    reserved.insert(token);
  }
  TextureNames result;
  for (const auto& [key, label] : value.asObject()) {
    const auto guid = as::parseAssetGuid(key);
    if (guid.isErr() || as::toString(guid.value()) != key || !colors.contains(guid.value()) ||
        !label.isString() || !ca::validImportName(label.asString()) ||
        !reserved.emplace(label.asString()).second || !result.emplace(guid.value(), label.asString()).second)
      return std::nullopt;
  }
  return value == nameValue(result) ? std::optional{std::move(result)} : std::nullopt;
}
std::string nameBytes(const TextureNames& names) {
  return names.empty() ? std::string{} : json::write(nameValue(names));
}
using TextureLocations = TextureNames;

std::string fallbackPath(std::string_view token) {
  return "textures/" + std::string(token) + ".png";
}
std::string effectiveLogicalPath(const TextureLocations& locations, as::AssetGuid guid, std::string_view token) {
  const auto found = locations.find(guid);
  return found == locations.end() ? fallbackPath(token) : found->second;
}
bool pathClaimed(const TextureLocations& locations, const Textures& textures, as::AssetGuid except,
                 std::string_view path) {
  return std::ranges::any_of(textures, [&](const auto& entry) {
    const auto& [token, unit] = entry;
    return unit.kind == TextureKind::Color && unit.guid != except && effectiveLogicalPath(locations, unit.guid, token) == path;
  });
}
std::optional<TextureLocations> parseLocations(const json::Value& value, const Textures& textures) {
  if (!value.isObject() || value.asObject().empty() || value.asObject().size() > kNameLimit)
    return std::nullopt;
  std::map<as::AssetGuid, std::string_view> colors;
  for (const auto& [token, unit] : textures)
    if (unit.kind == TextureKind::Color) colors.emplace(unit.guid, token);
  TextureLocations result;
  for (const auto& [key, path] : value.asObject()) {
    const auto guid = as::parseAssetGuid(key);
    if (guid.isErr() || as::toString(guid.value()) != key || !colors.contains(guid.value()) ||
        !path.isString() || !ca::validTextureLogicalPath(path.asString()) ||
        path.asString() == fallbackPath(colors.at(guid.value())) ||
        !result.emplace(guid.value(), path.asString()).second) return std::nullopt;
  }
  std::set<std::string> reserved;
  for (const auto& [guid, token] : colors)
    if (!reserved.emplace(effectiveLogicalPath(result, guid, token)).second) return std::nullopt;
  return value == nameValue(result) ? std::optional{std::move(result)} : std::nullopt;
}
bool absentFile(const fs::path& path) {
  return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
}
bool hasPendingTextureRecovery(const fs::path& project) {
  const auto pending = project / ".pending";
  if (absentFile(pending)) return false;
  if (!realDirectory(pending)) return true;
  std::error_code ec;
  for (fs::directory_iterator at(pending, ec), end; !ec && at != end; at.increment(ec)) {
    const auto name = utf8(at->path().filename());
    const auto id = as::parseAssetGuid(name);
    if (id.isErr() || as::toString(id.value()) != name) continue;
    if (!realDirectory(at->path()) || (!absentFile(at->path() / "removal.json") || !absentFile(at->path() / "move.json"))) return true;
  }
  return static_cast<bool>(ec);
}
bool labelClaimed(const TextureNames& names, const Textures& textures, as::AssetGuid except, std::string_view label) {
  const auto original = textures.find(label);
  if (original != textures.end() && original->second.kind == TextureKind::Color && original->second.guid != except)
    return true;
  return std::ranges::any_of(names, [&](const auto& entry) { return entry.first != except && entry.second == label; });
}
struct TextureEvidence {
  fs::path png, record;
  std::string digest, artifact, metadata;
  TextureKind kind;
};
using TextureInputs = std::map<std::string, TextureEvidence, std::less<>>;

std::string textureKey(std::string_view name, TextureKind kind) {
  return "content:texture_" + std::string(name) + "/" + textureLeaf(kind);
}
TextureKind textureKind(std::string_view key) {
  const auto leaf = key.substr(key.find_last_of('/') + 1);
  const auto found = std::ranges::find(kTextureLeaves, leaf);
  return found == kTextureLeaves.end() ? TextureKind::Color : static_cast<TextureKind>(found - kTextureLeaves.begin());
}
std::optional<std::string_view> textureName(std::string_view key) {
  constexpr std::string_view prefix = "content:texture_";
  const auto suffix = std::string("/") + textureLeaf(textureKind(key));
  if (!key.starts_with(prefix) || !key.ends_with(suffix) || key.size() <= prefix.size() + suffix.size()) return {};
  const auto name = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
  return ca::validImportName(name) ? std::optional{name} : std::nullopt;
}
fs::path textureDirectory(std::string_view name) { return fromUtf8("texture_" + std::string(name)); }

bool exactTexturePath(const fs::path& root, const fs::path& relative) {
  if (!safeRelative(root, relative)) return false;
  auto path = root;
  for (const auto& component : relative) {
    WIN32_FIND_DATAW data{};
    const auto next = path / component;
    const auto found = FindFirstFileW(next.c_str(), &data);
    if (found == INVALID_HANDLE_VALUE) return false;
    FindClose(found);
    if (component.native() != data.cFileName) return false;
    path = next;
  }
  return true;
}

std::optional<std::string> readNameFile(const fs::path& root, const fs::path& relative, DWORD limit = kNameBytes) {
  if (!exactTexturePath(root, relative)) return std::nullopt;
  const auto path = root / relative;
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!file || !GetFileInformationByHandle(file.get(), &info) || info.nFileSizeHigh || !info.nFileSizeLow ||
      info.nFileSizeLow > limit || info.nNumberOfLinks != 1 ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    return std::nullopt;
  std::string bytes(info.nFileSizeLow, '\0');
  DWORD size = 0;
  if (!ReadFile(file.get(), bytes.data(), info.nFileSizeLow, &size, nullptr) || size != info.nFileSizeLow)
    return std::nullopt;
  return bytes;
}

// An empty return value inside the optional means proven absence, never an empty file.
std::optional<std::string> readNameBytes(const fs::path& root) {
  if (absentFile(root / kTextureNames)) return std::string{};
  return readNameFile(root, fs::path(kTextureNames));
}

std::optional<std::string> readLocationBytes(const fs::path& root) {
  if (absentFile(root / kTextureLocations)) return std::string{};
  return readNameFile(root, fs::path(kTextureLocations), kLocationBytes);
}
std::optional<TextureLocations> readLocations(const fs::path& root, const Textures& textures) {
  const auto bytes = readLocationBytes(root);
  if (!bytes) return std::nullopt;
  if (bytes->empty()) return TextureLocations{};
  const auto json = connection::json::parse(*bytes);
  auto parsed = json.isOk() ? parseLocations(json.value(), textures) : std::nullopt;
  return parsed && nameBytes(*parsed) == *bytes ? parsed : std::nullopt;
}

std::optional<std::string> fileDigest(const fs::path& root, const fs::path& relative, DWORD limit,
                                      bool allowEmpty = false) {
  if (!exactTexturePath(root, relative)) return {};
  const auto path = root / relative;
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!file || !GetFileInformationByHandle(file.get(), &info) || info.nFileSizeHigh ||
      (!allowEmpty && !info.nFileSizeLow) ||
      info.nFileSizeLow > limit || info.nNumberOfLinks != 1 ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    return {};
  struct Sha {
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~Sha() {
      if (hash) BCryptDestroyHash(hash);
      if (provider) BCryptCloseAlgorithmProvider(provider, 0);
    }
  } sha;
  if (BCryptOpenAlgorithmProvider(&sha.provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptCreateHash(sha.provider, &sha.hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
  std::array<unsigned char, 64 * 1024> block{};
  DWORD remaining = info.nFileSizeLow;
  while (remaining) {
    DWORD got = 0;
    if (!ReadFile(file.get(), block.data(), std::min(remaining, static_cast<DWORD>(block.size())), &got, nullptr) ||
        got == 0 || BCryptHashData(sha.hash, block.data(), got, 0) < 0) return {};
    remaining -= got;
  }
  std::array<unsigned char, 32> digest{};
  if (BCryptFinishHash(sha.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
  std::string hex;
  for (const auto byte : digest) {
    constexpr std::string_view alphabet = "0123456789abcdef";
    hex.push_back(alphabet[byte >> 4]);
    hex.push_back(alphabet[byte & 15]);
  }
  return hex;
}

std::optional<TextureUnit> readTextureUnit(const fs::path& root, const fs::path& unit, std::string_view name) {
  if (!ca::validImportName(name) || !exactTexturePath(root, unit / "source.sha256") ||
      !exactTexturePath(root, unit / "catalog-source.txt")) return {};
  const auto fragment = readMaterialText(root, unit / "catalog-source.txt");
  if (!fragment) return {};
  const auto parsed = as::parseAssetCatalogSource(*fragment);
  if (parsed.isErr() || parsed.value().entries().size() != 1) return {};
  const auto& entry = parsed.value().entries().front();
  const auto key = "content:" + entry.path;
  const auto kind = textureKind(key);
  if (entry.type != as::CatalogAssetType::Texture || entry.root != as::CatalogRoot::Content || key != textureKey(name, kind)) return {};
  for (const auto* leaf : kTextureLeaves) {
    if (std::string_view(leaf) == textureLeaf(kind)) continue;
    const DWORD attributes = GetFileAttributesW((root / unit / leaf).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND) return {};
  }
  const auto digest = fileDigest(root, unit / textureLeaf(kind), kPngBytes);
  const auto seal = readMaterialText(root, unit / "source.sha256");
  if (!digest || !seal || *seal != *digest + "\n") return {};
  return TextureUnit{entry.guid, *digest, kind};
}

std::optional<Textures> registeredTextures(const fs::path& root) {
  const auto registry = root / "registered";
  if (!realDirectory(registry)) return {};
  Textures out;
  std::set<as::AssetGuid> identities;
  std::error_code ec;
  for (fs::directory_iterator at(registry, ec), end; !ec && at != end; at.increment(ec)) {
    const auto leaf = utf8(at->path().filename());
    if (!leaf.starts_with("texture_")) continue;
    const auto name = std::string_view(leaf).substr(8);
    const auto entry = readTextureUnit(registry, fromUtf8(leaf), name);
    if (!entry || !identities.insert(entry->guid).second) return {};
    out.emplace(name, *entry);
  }
  return ec ? std::nullopt : std::optional{std::move(out)};
}

std::optional<TextureInputs> textureInputs(const fs::path& root, const Producers& producers,
                                          const as::AssetCatalog& catalog) {
  TextureInputs out;
  std::set<std::string_view> names;
  for (const auto& [key, producer] : producers) {
    const auto name = textureName(key);
    if (!name) continue;
    if (!names.insert(*name).second) return {};
    const auto kind = textureKind(key);
    const auto* record = catalog.find(producer.root);
    if (!record || producer.type != as::CatalogAssetType::Texture || record->type != producer.type ||
        as::toString(record->source) != key || !record->dependencies.empty()) return {};
    const auto base = fs::path("assets") / textureDirectory(*name);
    const auto png = fs::path(kTextureSources) / textureDirectory(*name) / textureLeaf(kind);
    auto evidence = base;
    evidence += ".texture-evidence";
    auto stamp = base;
    stamp += ".stamp";
    if (!exactTexturePath(root, evidence)) return {};
    const auto payload = readMaterialText(root, evidence);
    if (utf8(producer.stamp) != utf8(stamp) || !exactTexturePath(root, stamp) || !payload || payload->size() != 195) return {};
    const auto digest = fileDigest(root, png, kPngBytes);
    const auto artifact = fileDigest(root, fromUtf8(record->artifact).lexically_relative(root), as::kPreviewBaseColorMaxArtifactBytes);
    const auto metadata = fileDigest(root, fromUtf8(record->metadata).lexically_relative(root), 64 * 1024);
    if (!digest || !artifact || !metadata || *payload != *digest + "\n" + *artifact + "\n" + *metadata + "\n")
      return {};
    auto loaded = material_authoring::texture(catalog, producer.root, textureColor(kind));
    if (loaded.isErr() || loaded.value().mipCount != 1) return {};
    out.emplace(key, TextureEvidence{png, evidence, *digest, *artifact, *metadata, kind});
  }
  for (const auto& record : catalog.records()) {
    const auto key = as::toString({record.source.root, record.source.path, {}});
    if (textureName(key) && !out.contains(as::toString(record.source))) return {};
  }
  return out;
}

Textures textureRegistrations(const Producers& producers, const TextureInputs& inputs) {
  Textures out;
  for (const auto& [key, entry] : inputs)
    out.emplace(*textureName(key), TextureUnit{producers.at(key).root, entry.digest, entry.kind});
  return out;
}

std::string managedSourceKey(std::string_view name) {
  return "content:managed_" + std::string(name) + "/source.gltf";
}
std::optional<std::string_view> managedName(std::string_view source) {
  constexpr std::string_view prefix = "content:managed_";
  constexpr std::string_view suffix = "/source.gltf";
  if (!source.starts_with(prefix) || !source.ends_with(suffix)) return std::nullopt;
  const auto name = source.substr(prefix.size(), source.size() - prefix.size() - suffix.size());
  if (!ca::validImportName(name)) return std::nullopt;
  return name;
}
Registrations managedRegistrations(const Producers& producers) {
  Registrations result;
  for (const auto& [key, producer] : producers)
    if (const auto name = managedName(key)) result.emplace(*name, producer.root);
  return result;
}
Producers baseProducers(Producers producers) {
  std::erase_if(producers, [](const auto& row) { return managedName(row.first) || materialName(row.first) || textureName(row.first); });
  return producers;
}
std::string_view inventoryHeader(std::string_view data) {
  return data.substr(0, data.find('\n'));
}

std::optional<Registrations> readRegistrations(const fs::path& root) {
  const auto registry = root / "registered";
  if (!realDirectory(registry)) return std::nullopt;
  Registrations result;
  std::error_code ec;
  for (fs::directory_iterator at(registry, ec), end; !ec && at != end; at.increment(ec)) {
    const auto dir = at->path();
    const auto leaf = utf8(dir.filename());
    if (leaf.starts_with("material_") || leaf.starts_with("texture_")) continue;
    if (!leaf.starts_with("managed_") || !ca::validImportName(std::string_view(leaf).substr(8)) ||
        !realDirectory(dir) || !safeRelative(dir, "source.gltf") || !safeRelative(dir, "catalog-source.txt"))
      return std::nullopt;
    const auto text = readText(dir / "catalog-source.txt");
    if (!text) return std::nullopt;
    const auto catalog = as::parseAssetCatalogSource(*text);
    if (catalog.isErr() || catalog.value().entries().size() != 1) return std::nullopt;
    const auto& entry = catalog.value().entries().front();
    if (entry.root != as::CatalogRoot::Content || entry.type != as::CatalogAssetType::GltfImport ||
        entry.path != leaf + "/source.gltf") return std::nullopt;
    result.emplace(leaf.substr(8), entry.guid);
  }
  if (ec) return std::nullopt;
  return result;
}

std::optional<Producers> readManagedView(const fs::path& root, const fs::path& inventory,
                                       std::optional<std::string_view> expectedHeader = std::nullopt) {
  if (!safeRelative(root, inventory) || !safeRelative(root, fs::path(kManagedSource))) return std::nullopt;
  const auto producerText = readText(root / inventory);
  const auto catalogText = readText(root / kManagedSource);
  if (!producerText || !catalogText || (expectedHeader && inventoryHeader(*producerText) != *expectedHeader))
    return std::nullopt;
  auto producers = readProducers(*producerText);
  const auto catalog = as::parseAssetCatalogSource(*catalogText);
  if (!producers || catalog.isErr()) return std::nullopt;
  for (const auto& [source, producer] : *producers) {
    const auto key = as::parseCatalogSourceId(source);
    const auto* entry = key ? catalog.value().find(key->root, key->path) : nullptr;
    if (!entry || entry->guid != producer.root || entry->type != producer.type) return std::nullopt;
  }
  for (const auto& entry : catalog.value().entries()) {
    const auto key = "content:" + entry.path;
    if (entry.root == as::CatalogRoot::Content && textureName(key) && !producers->contains(key)) return {};
  }
  return producers;
}

bool matchesManagedCatalog(const as::AssetCatalog& catalog, const Producers& producers) {
  const auto registered = managedRegistrations(producers);
  for (const auto& [name, root] : registered) {
    const auto* mesh = catalog.find(as::deriveAssetGuid(root, "mesh/0/0"));
    const auto* scene = catalog.find(as::deriveAssetGuid(root, "scene"));
    const auto key = managedSourceKey(name);
    if (!mesh || mesh->type != as::CatalogAssetType::Mesh || as::toString(mesh->source) != key + "#mesh/0/0" ||
        !scene || scene->type != as::CatalogAssetType::Scene || as::toString(scene->source) != key + "#scene")
      return false;
  }
  for (const auto& record : catalog.records()) {
    const auto source = as::toString({record.source.root, record.source.path, {}});
    if (auto name = managedName(source)) {
      const auto root = registered.find(*name);
      if (root == registered.end() || record.source.subKey.empty() ||
          as::deriveAssetGuid(root->second, record.source.subKey) != record.guid) return false;
    }
    if (materialName(source)) {
      const auto producer = producers.find(source);
      if (producer == producers.end() || producer->second.type != as::CatalogAssetType::Material ||
          record.type != as::CatalogAssetType::Material || !record.source.subKey.empty() ||
          producer->second.root != record.guid) return false;
    }
  }
  for (const auto& [key, producer] : producers) {
    if (!materialName(key)) continue;
    const auto* record = catalog.find(producer.root);
    if (!record || record->type != as::CatalogAssetType::Material || as::toString(record->source) != key) return false;
  }
  return true;
}

bool exactImportDelta(const fs::path& staging, const as::AssetCatalog& active, const as::AssetCatalog& staged,
                      const Producer& producer, std::string_view key) {
  const auto fragment = producer.generated.parent_path().parent_path() / "import.catalog.txt";
  if (!safeRelative(staging, fragment)) return false;
  const auto text = readText(staging / fragment);
  if (!text) return false;
  const auto records = as::parseAssetCatalogRecords(*text);
  if (records.isErr() || !retainsIdentity(active, staged) || active.size() + records.value().size() != staged.size())
    return false;
  for (const auto& record : records.value()) {
    const auto* added = staged.find(record.guid);
    if (!added || active.find(record.guid) || added->type != record.type ||
        as::toString(added->source) != as::toString(record.source) ||
        as::toString({record.source.root, record.source.path, {}}) != key ||
        record.source.subKey.empty() || as::deriveAssetGuid(producer.root, record.source.subKey) != record.guid)
      return false;
  }
  return true;
}

using Files = std::vector<fs::path>;
std::optional<Files> outputFiles(const fs::path& root, const as::AssetCatalog& catalog, const Producers& producers) {
  Files files{fs::path(kCatalog)};
  for (const auto& record : catalog.records()) {
    files.push_back(fromUtf8(record.artifact).lexically_relative(root));
    files.push_back(fromUtf8(record.metadata).lexically_relative(root));
  }
  for (const auto& [key, producer] : producers) {
    if (!producer.generated.empty()) files.push_back(producer.generated);
  }
  for (const auto& file : files) {
    if (!safeRelative(root, file) || sameComponent(file, fs::path(kManifest))) return std::nullopt;
  }
  return files;
}

json::Value generatedSources(const Producers& producers) {
  auto value = json::Value::object();
  for (const auto& [key, producer] : producers)
    if (!producer.generated.empty()) value.set(as::toString(producer.root), json::Value::string(utf8(producer.generated)));
  return value;
}
json::Value manifest(std::string_view owner, ca::GenerationId id, const Producers& producers,
                     const fs::path& managedInventory = {}, const Materials& materials = {},
                     const TextureInputs& textures = {}, const TextureNames& names = {},
                     const TextureLocations& locations = {}) {
  auto value = json::Value::object();
  value.set("profile", json::Value::string(std::string(owner)));
  value.set("generation", json::Value::string(as::toString(id.value)));
  value.set("importedSources", generatedSources(producers));
  const bool hasNormals = std::ranges::any_of(textures, [](const auto& entry) {
    return entry.second.kind == TextureKind::Normal;
  });
  const bool hasPackedMaps = std::ranges::any_of(textures, [](const auto& entry) {
    return entry.second.kind == TextureKind::MetallicRoughness;
  });
  const bool hasOcclusion = std::ranges::any_of(textures, [](const auto& entry) {
    return entry.second.kind == TextureKind::Occlusion;
  });
  if (!managedInventory.empty()) {
    const std::uint64_t version = !locations.empty() ? 9u : !names.empty() ? 8u : hasOcclusion ? 7u : hasPackedMaps ? 6u :
        hasNormals ? 5u : !textures.empty() ? 4u : materials.empty() ? 2u : 3u;
    value.set("version", json::Value::number(version));
    value.set("catalogSource", json::Value::string(std::string(kManagedSource)));
    value.set("producers", json::Value::string(utf8(managedInventory)));
  }
  if (!materials.empty()) {
    auto sources = json::Value::object();
    for (const auto& [name, unit] : materials) sources.set(materialKey(name), json::Value::string(unit.source));
    value.set("materialSources", std::move(sources));
  }
  if (!textures.empty()) {
    auto sources = json::Value::object();
    for (const auto& [key, input] : textures) {
      auto entry = json::Value::object();
      entry.set("guid", json::Value::string(as::toString(producers.at(key).root)));
      entry.set("png", json::Value::string(utf8(input.png)));
      entry.set("inputSha256", json::Value::string(input.digest));
      entry.set("artifactSha256", json::Value::string(input.artifact));
      entry.set("metadataSha256", json::Value::string(input.metadata));
      if (hasNormals || hasPackedMaps || hasOcclusion)
        entry.set("colorSpace", json::Value::string(input.kind == TextureKind::Color ? "Srgb" : "Unorm"));
      if (hasPackedMaps || hasOcclusion)
        entry.set("role", json::Value::string(kTextureRoles[static_cast<std::size_t>(input.kind)]));
      sources.set(key, std::move(entry));
    }
    value.set("textureSources", std::move(sources));
  }
  if (!names.empty()) value.set("textureDisplayNames", nameValue(names));
  if (!locations.empty()) value.set("textureLocations", nameValue(locations));
  return value;
}

struct End {
  ca::RebuildFailure failure;
  bool repair;
};
End fail(Stage stage, Code code, std::string text, bool repair = false,
         std::optional<std::uint32_t> exit = std::nullopt) {
  return {{stage, code, std::move(text), exit}, repair};
}
End timedOut(Stage stage) { return fail(stage, Code::TimedOut, "Asset rebuild deadline elapsed or the host stopped.", true); }

// No shell or request-supplied arguments. Config and paths are trusted local
// configuration, but still quoted for Windows argv parsing.
std::wstring quote(std::wstring_view arg) {
  std::wstring out = L"\"";
  std::size_t slashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') { ++slashes; continue; }
    out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    out.push_back(c);
  }
  out.append(slashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}
std::optional<bool> descendantsStopped(const Handle& job) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
  if (!QueryInformationJobObject(job.get(), JobObjectBasicAccountingInformation, &info,
                                 static_cast<DWORD>(sizeof(info)), nullptr)) return std::nullopt;
  return info.ActiveProcesses == 0;
}
template<class Interrupted>
std::optional<End> runBuild(const fs::path& cmake, const fs::path& staging, const std::string& configuration,
                            Interrupted interrupted, bool configure = false) {
  if (interrupted()) return timedOut(Stage::Pipeline);
  const auto launchFailed = [] { return fail(Stage::Launch, Code::LaunchFailed, "Cannot start contained CMake asset build."); };
  Handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                       static_cast<DWORD>(sizeof(limits)))) return launchFailed();
  SECURITY_ATTRIBUTES inheritable{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE};
  const fs::path logPath = staging / (configure ? "asset-configure.log" : "asset-rebuild.log");
  if (!safeRelative(staging, logPath.filename(), true)) return launchFailed();
  Handle log(CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS, 0, nullptr));
  Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));
  if (!log || !input) return launchFailed();
  std::wstring command = quote(cmake.native());
  if (configure) {
    command += L" " + quote(staging.native());
  } else {
    command += L" --build " + quote(staging.native()) + L" --config " +
        quote(fromUtf8(configuration).native()) + L" --target atlantis_asset_catalog";
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input.get();
  startup.hStdOutput = startup.hStdError = log.get();
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(cmake.c_str(), command.data(), nullptr, nullptr, TRUE,
                      CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, staging.c_str(), &startup, &process))
    return launchFailed();
  Handle child(process.hProcess), thread(process.hThread);
  if (!AssignProcessToJobObject(job.get(), child.get()) || ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
    TerminateProcess(child.get(), 1);
    WaitForSingleObject(child.get(), INFINITE);
    return launchFailed();
  }
  for (;;) {
    const auto stopped = descendantsStopped(job);
    if (!stopped) return fail(Stage::Pipeline, Code::BuildFailed, "Cannot inspect CMake descendants.", true);
    if (*stopped) break;
    if (interrupted()) {
      if (!TerminateJobObject(job.get(), 1))
        return fail(Stage::Pipeline, Code::BuildFailed, "Cannot settle CMake descendants.", true);
      for (;;) {
        const auto settled = descendantsStopped(job);
        if (!settled) return fail(Stage::Pipeline, Code::BuildFailed, "Cannot verify CMake descendants settled.", true);
        if (*settled) break;
        Sleep(20);
      }
      return timedOut(Stage::Pipeline);
    }
    Sleep(20);
  }
  DWORD exit = 0;
  if (!GetExitCodeProcess(child.get(), &exit))
    return fail(Stage::Pipeline, Code::BuildFailed, "Cannot read CMake exit status.", true);
  if (exit != 0) return fail(Stage::Pipeline, Code::BuildFailed,
      configure ? "Managed CMake configure failed. See asset-configure.log."
                : "The asset catalog target failed. See asset-rebuild.log.",
      false, static_cast<std::uint32_t>(exit));
  return std::nullopt;
}

template<class Interrupted>
bool copyOutput(const fs::path& from, const fs::path& to, Interrupted interrupted) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  if (ec) return false;
  std::ifstream input(from, std::ios::binary);
  std::ofstream output(to, std::ios::binary);
  if (!input || !output) return false;
  std::array<char, 64 * 1024> block{};
  while (input && !interrupted()) {
    input.read(block.data(), block.size());
    output.write(block.data(), input.gcount());
    if (!output) return false;
  }
  output.close();
  return input.eof() && !input.bad() && output && !interrupted();
}
bool createMarker(const fs::path& path, std::string_view text) {
  Handle marker(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
  DWORD written = 0;
  return marker && (text.empty() ||
      (WriteFile(marker.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
       written == text.size())) && FlushFileBuffers(marker.get());
}

template<class Interrupted>
bool sameOutput(const fs::path& a, const fs::path& b, Interrupted interrupted) {
  std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
  if (!left || !right) return false;
  std::array<char, 64 * 1024> x{}, y{};
  do {
    if (interrupted()) return false;
    left.read(x.data(), x.size());
    right.read(y.data(), y.size());
    if (left.gcount() != right.gcount() ||
        !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) return false;
  } while (left && right);
  return left.eof() && right.eof() && !left.bad() && !right.bad();
}

template<class Interrupted>
bool copyInbox(const fs::path& from, const fs::path& to, Interrupted interrupted, DWORD maxSize = 16 * 1024 * 1024) {
  Handle input(CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!input || !GetFileInformationByHandle(input.get(), &info) || info.nFileSizeHigh ||
      !info.nFileSizeLow || info.nFileSizeLow > maxSize || info.nNumberOfLinks != 1 ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
  std::ofstream output(to, std::ios::binary);
  if (!output) return false;
  std::array<char, 64 * 1024> block{};
  DWORD remaining = info.nFileSizeLow;
  while (remaining && !interrupted()) {
    DWORD size = 0;
    if (!ReadFile(input.get(), block.data(), std::min(remaining, static_cast<DWORD>(block.size())), &size, nullptr) ||
        !size || !output.write(block.data(), size)) return false;
    remaining -= size;
  }
  output.close();
  return !remaining && output && !interrupted();
}
}  // namespace

struct AssetRebuildHost::State {
  using RemovalInputs = std::map<fs::path, std::string>;
  struct RemovalRead {
    ca::TextureRemovalCheck check;
    std::string token;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
  };
  struct RemovalProof {
    RemovalInputs inputs;
    Producers view;
    Clock::time_point expires;
  };
  struct Work {
    ca::RebuildJob job;
    const Producer* producer;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
    std::optional<std::string> colorReplacement;
    std::optional<ca::RenameTextureRequest> rename{};
    std::optional<RemovalProof> removal{};
    std::optional<ca::MoveTextureRequest> move{};
  };

  struct ImportWork {
    ca::ImportJob job;
    std::optional<as::AssetGuid> registeredRoot;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
  };
  struct MaterialWork {
    ca::MaterialJob job;
    ca::MaterialSave request;
    ca::MaterialRecipeVersion version;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
  };
  struct TextureWork {
    ca::TextureImportJob job;
    TextureKind kind;
    std::optional<TextureUnit> resumeInput;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
  };

  Config config;
  fs::path staging, publication, active, inventoryPath, inventoryRelative, project;
  Handle projectWriter;
  Registrations registered, activeRegistered;
  std::optional<ImportWork> importing;
  std::deque<ca::ImportJob> imported;
  Materials activeMaterials, committedMaterials;
  std::optional<MaterialWork> materialWork;
  std::deque<ca::MaterialJob> materialJobs;
  Textures activeTextures, committedTextures;
  TextureNames activeNames, committedNames;
  TextureLocations activeLocations, committedLocations;
  bool textureRegistryChanged = false;
  std::optional<TextureWork> textureWork;
  std::deque<std::pair<TextureKind, ca::TextureImportJob>> textureJobs;
  std::string inventory, owner;
  Producers producers;
  Handle writer;
  std::atomic<bool> stopping = false;
  bool dirty = false;
  mutable std::mutex mutex;
  std::mutex publicationCommit;
  std::condition_variable changed;
  std::optional<Work> running;
  std::optional<RemovalRead> removalCheck;
  std::optional<RemovalProof> removalProof;
  std::deque<ca::RebuildJob> completed;
  std::thread worker;

  ~State() { stop(); }
  void stop() {
    {
      std::lock_guard commit(publicationCommit);
      std::lock_guard lock(mutex);
      stopping = true;
    }
    changed.notify_one();
    if (worker.joinable()) worker.join();
  }

  std::optional<ca::RebuildError> slotError() const {
    if (stopping) return ca::RebuildError::Unavailable;
    if (!writer || (!project.empty() && !projectWriter) || running || importing || materialWork || textureWork ||
        (removalCheck && removalCheck->check.state == ca::TextureRemovalState::Checking))
      return ca::RebuildError::Busy;
    if (dirty) return ca::RebuildError::WorkspaceNeedsRepair;
    return std::nullopt;
  }

  std::optional<ca::RebuildError> admissionLocked() const {
    if (auto error = slotError()) return error;
    if (!project.empty() && (registered != activeRegistered || committedMaterials != activeMaterials ||
        textureRegistryChanged || metadataChanged()))
      return ca::RebuildError::Unavailable;
    return std::nullopt;
  }

  bool namesMatch(const TextureNames& names) const {
    return project.empty() || readNameBytes(project) == nameBytes(names);
  }
  bool locationsMatch(const TextureLocations& locations) const {
    return project.empty() || readLocationBytes(project) == nameBytes(locations);
  }
  bool metadataMatches() const {
    return namesMatch(activeNames) && locationsMatch(activeLocations);
  }

  bool metadataChanged() const {
    return committedNames != activeNames || committedLocations != activeLocations;
  }

  struct Prepared {
    ca::GenerationId id;
    fs::path directory;
    json::Value expectedManifest{};
  };
  template<class Interrupted>
  std::variant<Prepared, End> prepare(const as::AssetCatalog& catalog, const Producers& view, Interrupted interrupted,
                                      std::optional<Prepared>* ownedStaging = nullptr, const TextureNames* rename = nullptr,
                                      Files* cloned = nullptr, const TextureNames* removalNames = nullptr,
                                      const TextureLocations* movedLocations = nullptr,
                                      const TextureLocations* removalLocations = nullptr) {
    const auto root = rename || movedLocations ? active.parent_path() : staging;
    const auto& names = rename ? *rename : removalNames ? *removalNames : activeNames;
    const auto& locations = movedLocations ? *movedLocations : removalLocations ? *removalLocations : activeLocations;
    auto files = outputFiles(root, catalog, view);
    if (!files) return fail(Stage::Validation, Code::InvalidOutput, "Staging output is missing or unsafe.", true);
    Materials materials;
    TextureInputs textures;
    if (!project.empty()) {
      files->push_back(inventoryRelative);
      files->push_back(fs::path(kManagedSource));
      auto inputs = materialInputs(root, view);
      if (!inputs) return fail(Stage::Validation, Code::InvalidOutput, "Invalid staged material inputs.", true);
      materials = std::move(*inputs);
      for (const auto& [name, unit] : materials)
        files->push_back(fs::path(kMaterialSources) / materialDirectory(name) / kMaterialLeaf);
      auto input = textureInputs(root, view, catalog);
      if (!input) return fail(Stage::Validation, Code::InvalidOutput, "Invalid staged texture evidence.", true);
      textures = std::move(*input);
      if (!names.empty() && !parseNames(nameValue(names), textureRegistrations(view, textures)))
        return fail(Stage::Validation, Code::InvalidOutput, "Prepared catalog does not preserve the display-name registry.");
      if (!locations.empty() && !parseLocations(nameValue(locations), textureRegistrations(view, textures)))
        return fail(Stage::Validation, Code::InvalidOutput, "Prepared catalog does not preserve the texture locations.");
      for (const auto& [key, texture] : textures) {
        files->push_back(texture.png);
        files->push_back(texture.record);
        files->push_back(view.at(key).stamp);
      }
    }
    if (interrupted()) return timedOut(Stage::Validation);
    const auto guid = newGuid();
    if (!guid) return fail(Stage::Publication, Code::PublishFailed, "Cannot allocate a generation ID.");
    const ca::GenerationId id{*guid};
    const fs::path temp = publication / (".pending-" + as::toString(*guid));
    std::error_code ec;
    if (!fs::create_directory(temp, ec) || ec)
      return fail(Stage::Publication, Code::PublishFailed, "Cannot create publication staging.");
    if (ownedStaging) ownedStaging->emplace(Prepared{id, temp});
    for (const auto& file : *files) {
      if (!safeRelative(root, file) || !copyOutput(root / file, temp / file, interrupted))
        return interrupted() ? timedOut(Stage::Publication)
                             : fail(Stage::Publication, Code::PublishFailed, "Cannot copy a complete generation.");
    }
    const auto data = manifest(owner, id, view, project.empty() ? fs::path{} : inventoryRelative, materials, textures, names, locations);
    if (!writeText(temp / kManifest, json::write(data)))
      return fail(Stage::Publication, Code::PublishFailed, "Cannot write generation manifest.");
    if (!textures.empty()) {
      const auto copiedCatalog = readCatalog(temp);
      const auto copiedInput = copiedCatalog ? textureInputs(temp, view, *copiedCatalog) : std::nullopt;
      if (!copiedInput || manifest(owner, id, view, inventoryRelative, materials, *copiedInput, names, locations) != data)
        return fail(Stage::Validation, Code::InvalidOutput, "Prepared texture generation differs from staging.", true);
    }
    if (cloned) *cloned = std::move(*files);
    return Prepared{id, temp, data};
  }

  bool publish(const Prepared& generation, const TextureNames* renamed = nullptr,
               const TextureLocations* moved = nullptr) const {
    if (!namesMatch(renamed ? *renamed : activeNames) || !locationsMatch(moved ? *moved : activeLocations) || !safeRelative(generation.directory, fs::path(kManifest)) ||
        readJson(generation.directory / kManifest) != generation.expectedManifest) return false;
    return MoveFileExW(generation.directory.c_str(),
                      (publication / as::toString(generation.id.value)).c_str(), 0);
  }

  bool ordinaryRemovalProfile() const {
    if (!safeRelative(staging, fs::path(kRemovalProfile))) return false;
    const auto profile = readText(staging / kRemovalProfile);
    return profile == "ordinary\n" || profile == "ordinary\r\n";
  }

  bool inactiveFixtureMaterial(std::string_view key) const {
    // The asset declarations include this recipe even in ordinary builds
    // configured without the test targets. Its source is still checked.
    if (key != "assets:_test_fixtures/cmake_material_declaration_test.material.txt") return false;
    const auto cache = readText(staging / "CMakeCache.txt");
    return cache && (cache->find("\nATLANTIS_BUILD_TESTS:BOOL=OFF\n") != std::string::npos ||
                     cache->find("\nATLANTIS_BUILD_TESTS:BOOL=OFF\r\n") != std::string::npos);
  }

  std::optional<End> removalInputs(const Producers& roster, RemovalInputs& inputs,
                                   const auto& interrupted, ca::TextureRemovalCheck* check = nullptr) const {
    constexpr DWORD maxInput = 256 * 1024 * 1024;
    constexpr std::size_t maxFiles = 32768;
    auto header = columns(std::string(inventoryHeader(inventory)));
    const auto unverified = [](std::string reason) {
      return fail(Stage::Validation, Code::InvalidOutput, std::move(reason));
    };
    if (header.size() < 5) return unverified("Incomplete configured producer identity.");
    if (!header.back().empty() && header.back().back() == '\r') header.back().pop_back();
    const auto checkout = fromUtf8(header[1]).make_preferred();
    if (!checkout.is_absolute() || !safeProjectRoot(checkout, checkout))
      return unverified("The configured checkout is missing or unsafe.");
    const auto bistro = header.size() > 5 ? fs::path(fromUtf8(header[5]).make_preferred()) : fs::path{};
    const auto remember = [&](const fs::path& root, const fs::path& relative) {
      if (interrupted() || inputs.size() >= maxFiles) return false;
      const auto digest = fileDigest(root, relative, maxInput, true);
      return digest && inputs.try_emplace(root / relative, *digest).first->second == *digest;
    };
    bool incomplete = false;
    for (const auto& path : {fs::path("CMakeLists.txt"), fs::path("assets/CMakeLists.txt"),
          fs::path("assets/asset_catalog.txt"), fs::path("src/asset_system/CMakeLists.txt"),
          fs::path("cmake/AtlantisManagedImports.cmake"), fs::path("src/tools/gltf_importer/CMakeLists.txt")})
      if (!remember(checkout, path)) incomplete = true;
    if (!remember(staging, "CMakeCache.txt")) incomplete = true;

    auto sources = roster;
    constexpr std::string_view fixture = "assets:_test_fixtures/cmake_material_declaration_test.material.txt";
    if (!sources.contains(fixture) && inactiveFixtureMaterial(fixture)) {
      const auto text = readMaterialText(checkout, "assets/asset_catalog.txt");
      if (!text) return unverified("Cannot read declared material sources.");
      const auto catalog = as::parseAssetCatalogSource(*text);
      const auto* entry = catalog.isOk()
          ? catalog.value().find(as::CatalogRoot::Assets, fixture.substr(7)) : nullptr;
      if (!entry || entry->type != as::CatalogAssetType::Material)
        return unverified("Cannot verify the inactive material declaration.");
      sources.emplace(fixture, Producer{entry->guid, entry->type, {}, {}});
    }
    for (const auto& [key, producer] : sources) {
      if (interrupted()) return unverified("Reference checking did not finish before the deadline.");
      const auto source = as::parseCatalogSourceId(key);
      if (!source) return unverified("A configured source could not be resolved.");
      if (producer.type == as::CatalogAssetType::Material) {
        const auto name = materialName(key);
        if (!name && source->root != as::CatalogRoot::Assets) {
          incomplete = true;
          continue;
        }
        const auto root = name ? project / "registered" : checkout / "assets";
        const auto recipe = fromUtf8(source->path);
        const auto bytes = remember(root, recipe) ? readMaterialText(root, recipe) : std::nullopt;
        if (!bytes) {
          incomplete = true;
          continue;
        }
        const auto parsed = as::parseMaterialSource(*bytes);
        if (parsed.isErr()) {
          incomplete = true;
          continue;
        }
        using Slot = ca::TextureReferenceSlot;
        const auto inspect = [&](std::optional<as::AssetGuid> ref, Slot slot) {
          if (check && ref == check->target.asset) addRemovalBlocker(*check, {producer.root, key, slot});
        };
        const auto& material = parsed.value();
        inspect(material.textureAsset, Slot::BaseColor);
        inspect(material.normalMapAsset, Slot::Normal);
        inspect(material.metallicRoughnessMapAsset, Slot::MetallicRoughness);
        inspect(material.occlusionMapAsset, Slot::Occlusion);
        inspect(material.emissiveTextureAsset, Slot::Emissive);
      }
      if (materialName(key) || managedName(key) || textureName(key)) {
        const auto unit = fromUtf8(source->path).parent_path();
        if (!remember(project, fs::path("registered") / unit / "catalog-source.txt")) incomplete = true;
      }
      if (producer.type != as::CatalogAssetType::GltfImport) continue;
      const auto keyPath = fromUtf8(source->path);
      const auto root = managedName(key)
          ? project / "registered" / keyPath.parent_path()
          : bistro;
      if (root.empty() || (!managedName(key) &&
          (keyPath.parent_path() != root.filename() || source->root != as::CatalogRoot::Content)) ||
          !realDirectory(root) || !safeProjectRoot(root, root) || !exactTexturePath(root, keyPath.filename())) {
        incomplete = true;
        continue;
      }
      // The ordinary Bistro rule also reads this checkout file, outside
      // the imported content subtree. A current stamp cannot prove it.
      if (!managedName(key) && !remember(checkout, "assets/bistro/bistro_overlay.scene.txt")) incomplete = true;
      // The configured import owns this content subtree, including its
      // external buffers and images. Unknown roots cannot authorize removal.
      std::error_code ec;
      for (fs::recursive_directory_iterator at(root, ec), end; !ec && at != end; at.increment(ec)) {
        const DWORD attributes = GetFileAttributesW(at->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            interrupted() || inputs.size() >= maxFiles) {
          incomplete = true;
          break;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && !remember(root, at->path().lexically_relative(root))) {
          incomplete = true;
          break;
        }
      }
      if (ec) incomplete = true;
    }
    if (incomplete) return unverified("Cannot verify all configured material, import or build inputs.");
    return std::nullopt;
  }

  static void addRemovalBlocker(ca::TextureRemovalCheck& check, ca::TextureRemovalBlocker blocker) {
    if (std::ranges::find(check.blockers, blocker) != check.blockers.end()) return;
    if (check.blockers.size() == ca::kTextureRemovalBlockerLimit) {
      check.truncated = true;
      return;
    }
    check.blockers.push_back(std::move(blocker));
  }

  static void checkRemovalEdges(const as::AssetCatalog& catalog, ca::TextureRemovalCheck& check) {
    for (const auto& record : catalog.records()) {
      if (std::ranges::find(record.dependencies, check.target.asset) != record.dependencies.end())
        addRemovalBlocker(check, {record.guid, as::toString(record.source), {}});
    }
  }

  std::optional<Producers> removalView() const {
    const auto view = readManagedView(staging, inventoryRelative, inventoryHeader(inventory));
    if (!view || !ordinaryRemovalProfile()) return {};
    const auto sourceText = readText(staging / kManagedSource);
    if (!sourceText) return {};
    const auto source = as::parseAssetCatalogSource(*sourceText);
    if (source.isErr()) return {};
    for (const auto& entry : source.value().entries()) {
      const auto key = as::toString({entry.root, entry.path, {}});
      if ((entry.type == as::CatalogAssetType::Material || entry.type == as::CatalogAssetType::GltfImport) &&
          !view->contains(key) && !inactiveFixtureMaterial(key)) return {};
    }
    return view;
  }

  bool removalRegistryMatches() const {
    return readRegistrations(project) == activeRegistered && registeredMaterials(project) == activeMaterials &&
        registeredTextures(project) == activeTextures && metadataMatches();
  }

  bool safeRemovalUnit(const fs::path& unit, std::string_view token) const {
    if (!realDirectory(project / unit) || !safeRelative(project, unit / "source.png")) return false;
    std::set<std::string> entries;
    std::error_code ec;
    for (fs::directory_iterator at(project / unit, ec), end; !ec && at != end; at.increment(ec)) {
      const auto leaf = at->path().filename();
      if (!safeRelative(project, unit / leaf)) return false;
      entries.insert(utf8(leaf));
      if (entries.size() > 3) return false;
    }
    const auto original = activeTextures.find(token);
    if (ec || entries != std::set<std::string>{"catalog-source.txt", "source.png", "source.sha256"} ||
        original == activeTextures.end() || readTextureUnit(project, unit, token) != original->second ||
        !fileDigest(project, unit / "catalog-source.txt", 64 * 1024) ||
        !fileDigest(project, unit / "source.sha256", 64 * 1024)) return false;
    const auto expected = as::serializeAssetCatalogSource({{original->second.guid, as::CatalogAssetType::Texture,
        as::CatalogRoot::Content, textureKey(token, TextureKind::Color).substr(8)}});
    return readMaterialText(project, unit / "catalog-source.txt") == expected;
  }

  bool safeRemovalInbox(std::string_view token) const {
    const auto inbox = fs::path("inbox") / fromUtf8(token) / "source.png";
    if (!safeRelative(project, inbox, true)) return false;
    if (!fs::exists(project / inbox)) return true;
    Handle file(CreateFileW((project / inbox).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    return file && GetFileInformationByHandle(file.get(), &info) && info.nNumberOfLinks == 1 &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
  }

  void settleRemovalCheck(RemovalRead work) {
    using S = ca::TextureRemovalState;
    auto& check = work.check;
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const auto unit = fs::path("registered") / textureDirectory(work.token);
    bool repair = false;
    bool marked = false;
    std::optional<RemovalProof> proof;
    checkRemovalEdges(*work.catalog, check);
    const auto inspect = [&]() -> S {
      if (!safeRemovalUnit(unit, work.token) || !safeRemovalInbox(work.token)) {
        check.diagnostic = "The registered unit or original inbox path cannot be verified.";
        check.incomplete = true;
        return S::CheckIncomplete;
      }
      if (!safeBuildTree(staging, interrupted) || !safeRelative(staging, fs::path(kPendingSelection)) ||
          readText(staging / kPendingSelection) != "") {
        check.diagnostic = "Staging is unsafe or has a pending selection.";
        check.incomplete = true;
        repair = true;
        return S::CheckIncomplete;
      }
      if (!createMarker(staging / kDirty, {})) {
        check.diagnostic = "Cannot reserve staging for the check.";
        check.incomplete = true;
        repair = true;
        return S::CheckIncomplete;
      }
      marked = true;
      const auto configure = runBuild(config.cmake, staging, config.configuration, interrupted, true);
      const auto view = configure ? std::nullopt : removalView();
      if (!view) {
        check.diagnostic = "Cannot reconcile the current configured producer graph.";
        check.incomplete = true;
        repair = configure && configure->repair;
        return S::CheckIncomplete;
      }
      RemovalInputs before;
      if (auto error = removalInputs(*view, before, interrupted, &check)) {
        check.diagnostic = error->failure.diagnostic;
        check.incomplete = true;
        return S::CheckIncomplete;
      }
      if (!check.blockers.empty()) return S::Referenced;

      bool refreshImports = false;
      for (const auto& [key, entry] : *view) {
        if (entry.type != as::CatalogAssetType::GltfImport) continue;
        if (!safeRelative(staging, entry.stamp, true)) {
          check.diagnostic = "A generated producer stamp is unsafe.";
          check.incomplete = true;
          return S::CheckIncomplete;
        }
        std::error_code ec;
        fs::remove(staging / entry.stamp, ec);
        if (ec) {
          check.diagnostic = "Cannot obtain fresh importer evidence.";
          check.incomplete = true;
          return S::CheckIncomplete;
        }
        refreshImports = true;
      }
      if (refreshImports) {
        const auto built = runBuild(config.cmake, staging, config.configuration, interrupted);
        const auto catalog = built ? std::nullopt : readCatalog(staging);
        if (!catalog || removalView() != view || !matchesManagedCatalog(*catalog, *view)) {
          check.diagnostic = "Cannot validate current importer output.";
          check.incomplete = true;
          repair = built && built->repair;
          return S::CheckIncomplete;
        }
        checkRemovalEdges(*catalog, check);
      }
      RemovalInputs after;
      if (removalView() != view || removalInputs(*view, after, interrupted) || before != after ||
          *view != producers || !removalRegistryMatches() || !safeRemovalUnit(unit, work.token) ||
          !safeRemovalInbox(work.token) || interrupted()) {
        check.diagnostic = "Project sources or registrations differ from the active generation. Refresh and check again.";
        return S::StaleCheck;
      }
      proof.emplace(RemovalProof{std::move(after), *view, {}});
      return S::Ready;
    };
    try {
      check.state = inspect();
    } catch (const std::exception&) {
      check.state = S::CheckIncomplete;
      check.incomplete = true;
      check.diagnostic = "Cannot verify all project inputs.";
      repair = marked;
    }
    if (marked && !repair) repair = DeleteFileW((staging / kDirty).c_str()) == FALSE;
    if (repair) {
      check.state = S::CheckIncomplete;
      check.incomplete = true;
      check.diagnostic = "Cannot settle check staging. Inspect the workspace before retrying.";
    }
    if (!check.blockers.empty()) check.state = S::Referenced;
    if (check.state != S::Ready) proof.reset();
    else proof->expires = Clock::now() + std::chrono::minutes(2);
    std::lock_guard lock(mutex);
    dirty = dirty || repair;
    removalProof = std::move(proof);
    removalCheck->check = std::move(check);
  }

  std::variant<ca::GenerationId, End> removeColor(const Work& work, bool& cleanupRepair) {
    const auto& proof = *work.removal;
    const auto token = *work.colorReplacement;
    const auto key = textureKey(token, TextureKind::Color);
    const auto unit = fs::path("registered") / textureDirectory(token);
    const auto pending = fs::path(".pending") / as::toString(work.job.id.value);
    const auto backup = pending / "previous";
    const auto recoveryRecord = pending / "removal.json";
    const auto namePath = project / kTextureNames;
    const auto nameBefore = nameBytes(activeNames);
    auto remainingNames = activeNames;
    remainingNames.erase(work.job.asset);
    const auto nameAfter = nameBytes(remainingNames);
    const auto locationBefore = nameBytes(activeLocations);
    auto remainingLocations = activeLocations;
    remainingLocations.erase(work.job.asset);
    const auto locationAfter = nameBytes(remainingLocations);
    auto remainingTextures = activeTextures;
    remainingTextures.erase(token);
    auto remainingView = proof.view;
    remainingView.erase(key);
    const auto selection = "remove:" + as::toString(work.job.asset) + ":" + utf8(unit.filename());
    const auto selectionPath = staging / kPendingSelection;
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const auto stale = [] {
      return fail(Stage::Validation, Code::IdentityChanged,
                  "The checked inputs changed. Check this registration again before removing it.");
    };
    const auto unknown = [](std::string text) {
      return fail(Stage::Publication, Code::PublishFailed,
                  std::move(text) + " Keep the recovery record and inspect the workspace offline.", true);
    };
    const auto cleanTimeout = [] {
      return fail(Stage::Publication, Code::TimedOut, "Removal stopped before changing the registration.");
    };
    std::optional<Prepared> prepared;
    bool marked = false, ownsPending = false, ownsSelection = false;
    const auto checkedInputsMatch = [&](bool removed = false) {
      RemovalInputs current;
      if (removalInputs(removed ? remainingView : proof.view, current, interrupted)) return false;
      if (!removed) return current == proof.inputs;
      auto expected = proof.inputs;
      expected.erase(project / unit / "catalog-source.txt");
      return current == expected;
    };
    const auto registeredAfter = [&] {
      return absentFile(project / unit) && registeredTextures(project) == remainingTextures &&
          registeredMaterials(project) == activeMaterials && readRegistrations(project) == activeRegistered;
    };
    const auto move = [&](const fs::path& from, const fs::path& to) {
      return MoveFileExW((project / from).c_str(), (project / to).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    };
    const auto replaceMetadata = [&](bool location, std::string_view current, std::string_view intended,
                                     const fs::path& input) {
      const auto read = [&] { return location ? readLocationBytes(project) : readNameBytes(project); };
      const auto path = location ? project / kTextureLocations : namePath;
      if (read() != current) return false;
      if (current == intended) return true;
      if (intended.empty()) DeleteFileW(path.c_str());
      else {
        if (readNameFile(project, input, location ? kLocationBytes : kNameBytes) != intended) return false;
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (current.empty() ? 0 : MOVEFILE_REPLACE_EXISTING);
        MoveFileExW((project / input).c_str(), path.c_str(), flags);
      }
      return read() == intended;
    };
    const auto validateCatalog = [&](const as::AssetCatalog& catalog) {
      if (catalog.find(work.job.asset) || catalog.size() + 1 != work.catalog->size() ||
          !matchesManagedCatalog(catalog, remainingView)) return false;
      for (const auto& record : catalog.records()) {
        const auto* previous = work.catalog->find(record.guid);
        if (!previous || previous->type != record.type || as::toString(previous->source) != as::toString(record.source) ||
            previous->assetId != record.assetId || previous->dependencies != record.dependencies ||
            previous->artifactSchema != record.artifactSchema || previous->sourceSchema != record.sourceSchema ||
            previous->tool != record.tool || std::ranges::find(record.dependencies, work.job.asset) != record.dependencies.end())
          return false;
      }
      return true;
    };
    const auto execute = [&]() -> std::variant<ca::GenerationId, End> {
      if (interrupted()) return cleanTimeout();
      if (!safeBuildTree(staging, interrupted) || !safeRelative(staging, fs::path(kPendingSelection)) ||
          readText(selectionPath) != "") return unknown("Cannot establish staging ownership.");
      if (!removalRegistryMatches() || !safeRemovalUnit(unit, token) || !safeRemovalInbox(token) ||
          !checkedInputsMatch()) return stale();
      if (!createMarker(staging / kDirty, {}) ||
          !createMarker(project / kProjectDirty, utf8(staging) + "\n" + utf8(publication) + "\n"))
        return unknown("Cannot record both workspace markers.");
      marked = true;
      if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true)) return *error;
      if (removalView() != proof.view || !checkedInputsMatch()) return stale();
      std::error_code ec;
      if (absentFile(project / pending.parent_path())) fs::create_directory(project / pending.parent_path(), ec);
      if (ec || !realDirectory(project / pending.parent_path()) ||
          !fs::create_directory(project / pending, ec) || ec) return unknown("Cannot reserve private removal staging.");
      ownsPending = true;
      if (!createMarker(project / pending / "selection", selection))
        return fail(Stage::Launch, Code::LaunchFailed, "Cannot record the owned removal selection.");
      ownsSelection = true;
      if (!writeText(selectionPath, selection)) return unknown("Cannot set the removal selection.");
      if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true)) return *error;
      if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted)) return *error;
      const auto catalog = readCatalog(staging);
      const auto inputs = catalog ? textureInputs(staging, remainingView, *catalog) : std::nullopt;
      const auto oldRoot = active.parent_path();
      if (removalView() != remainingView || !catalog || !validateCatalog(*catalog) || !inputs ||
          textureRegistrations(remainingView, *inputs) != remainingTextures ||
          materialInputs(staging, remainingView) != activeMaterials || !matchesFrozenMaterials(*catalog, activeMaterials))
        return fail(Stage::Validation, Code::InvalidOutput, "Removal candidate differs from the intended catalog delta.");
      for (const auto& record : catalog->records()) {
        const auto* previous = work.catalog->find(record.guid);
        const auto sameFile = [&](std::string_view oldPath, std::string_view newPath) {
          const auto from = fromUtf8(oldPath);
          const auto to = fromUtf8(newPath);
          return safeRelative(oldRoot, from.lexically_relative(oldRoot)) &&
              safeRelative(staging, to.lexically_relative(staging)) && sameOutput(from, to, interrupted);
        };
        if (!sameFile(previous->artifact, record.artifact) || !sameFile(previous->metadata, record.metadata))
          return fail(Stage::Validation, Code::InvalidOutput, "A surviving cooked asset changed while building removal.");
      }
      auto result = prepare(*catalog, remainingView, interrupted, &prepared, nullptr, nullptr, &remainingNames, nullptr, &remainingLocations);
      if (const auto* error = std::get_if<End>(&result)) return *error;
      prepared = std::get<Prepared>(std::move(result));
      const auto final = publication / as::toString(prepared->id.value);
      RemovalInputs frozen;
      for (fs::recursive_directory_iterator at(prepared->directory, ec), end; !ec && at != end; at.increment(ec)) {
        if (at->is_directory(ec)) continue;
        const auto file = at->path().lexically_relative(prepared->directory);
        const auto digest = fileDigest(prepared->directory, file, 256 * 1024 * 1024, true);
        if (!digest || interrupted()) return fail(Stage::Validation, Code::InvalidOutput, "Cannot freeze the prepared generation.");
        frozen.emplace(file, *digest);
      }
      if (ec) return fail(Stage::Validation, Code::InvalidOutput, "Cannot enumerate the prepared generation.");
      const auto frozenMatches = [&](const fs::path& path) {
        if (!realDirectory(path) || readManagedView(path, inventoryRelative, inventoryHeader(inventory)) != remainingView ||
            !safeRelative(path, fs::path(kManifest)) || readJson(path / kManifest) != prepared->expectedManifest)
          return false;
        const auto candidate = readCatalog(path);
        const auto candidateInputs = candidate ? textureInputs(path, remainingView, *candidate) : std::nullopt;
        return candidate && validateCatalog(*candidate) && candidateInputs &&
            textureRegistrations(remainingView, *candidateInputs) == remainingTextures &&
            materialInputs(path, remainingView) == activeMaterials &&
            std::ranges::all_of(frozen, [&](const auto& file) {
              return fileDigest(path, file.first, 256 * 1024 * 1024, true) == file.second;
            });
      };
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.afterRemovalPreparation) config.afterRemovalPreparation(prepared->directory);
#endif
      std::lock_guard commit(publicationCommit);
      if (interrupted()) return cleanTimeout();
      if (!frozenMatches(prepared->directory) || readText(selectionPath) != selection ||
          !checkedInputsMatch() || !removalRegistryMatches() || !safeRemovalInbox(token) || !safeRemovalUnit(unit, token))
        return stale();
      if (!absentFile(final) || !absentFile(project / backup)) return unknown("Cannot prove the removal destinations are vacant.");
      auto record = json::Value::object();
      record.set("job", json::Value::string(as::toString(work.job.id.value)));
      record.set("asset", json::Value::string(as::toString(work.job.asset)));
      record.set("activeGeneration", json::Value::string(as::toString(config.generation->value)));
      record.set("generation", json::Value::string(as::toString(prepared->id.value)));
      record.set("unit", json::Value::string(utf8(unit)));
      record.set("backup", json::Value::string(utf8(backup)));
      record.set("digest", json::Value::string(activeTextures.at(token).digest));
      record.set("fragment", json::Value::string(as::serializeAssetCatalogSource({
          {work.job.asset, as::CatalogAssetType::Texture, as::CatalogRoot::Content, key.substr(8)}})));
      record.set("oldNames", json::Value::string(nameBefore));
      record.set("newNames", json::Value::string(nameAfter));
      record.set("oldLocations", json::Value::string(locationBefore));
      record.set("newLocations", json::Value::string(locationAfter));
      auto intended = json::Value::object();
      for (const auto& [file, digest] : frozen) intended.set(utf8(file), json::Value::string(digest));
      record.set("frozen", std::move(intended));
      if ((nameBefore != nameAfter &&
           ((!nameBefore.empty() && !createMarker(project / pending / "old-names", nameBefore)) ||
            (!nameAfter.empty() && !createMarker(project / pending / "new-names", nameAfter)))) ||
          (locationBefore != locationAfter &&
           ((!locationBefore.empty() && !createMarker(project / pending / "old-locations", locationBefore)) ||
            (!locationAfter.empty() && !createMarker(project / pending / "new-locations", locationAfter)))) ||
          !createMarker(project / recoveryRecord, json::write(record)))
        return fail(Stage::Publication, Code::PublishFailed, "Cannot flush the removal recovery record.");
      if (interrupted()) return cleanTimeout();
      const auto rollback = [&]() {
        const auto names = readNameBytes(project);
        const auto locations = readLocationBytes(project);
        if (!absentFile(final) || !registeredAfter() || !safeRemovalUnit(backup, token) ||
            (names != nameBefore && names != nameAfter) ||
            (names != nameBefore && !nameBefore.empty() && readNameFile(project, pending / "old-names") != nameBefore) ||
            (locations != locationBefore && locations != locationAfter) ||
            (locations != locationBefore && !locationBefore.empty() &&
             readNameFile(project, pending / "old-locations", kLocationBytes) != locationBefore))
          return unknown("Cannot prove that the previous registration can be restored.");
        move(backup, unit);
        if (!absentFile(project / backup) || !safeRemovalUnit(unit, token) ||
            !replaceMetadata(false, *names, nameBefore, pending / "old-names") ||
            !replaceMetadata(true, *locations, locationBefore, pending / "old-locations") || !removalRegistryMatches())
          return unknown("Cannot verify the restored registration and name map.");
        return fail(Stage::Publication, Code::PublishFailed, "The previous registration was restored. Check and retry.");
      };
      move(unit, backup);
      if (!registeredAfter() || !safeRemovalUnit(backup, token)) {
        if (absentFile(project / backup) && safeRemovalUnit(unit, token) && removalRegistryMatches())
          return fail(Stage::Publication, Code::PublishFailed, "Could not move the registration. Check and retry.");
        return unknown("Cannot verify the registration backup.");
      }
      if (!replaceMetadata(false, nameBefore, nameAfter, pending / "new-names") ||
          !replaceMetadata(true, locationBefore, locationAfter, pending / "new-locations")) return rollback();
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.beforeRemovalPublication) config.beforeRemovalPublication(prepared->directory, final);
#endif
      const auto committedMatches = [&] {
        return registeredAfter() && safeRemovalUnit(backup, token) && namesMatch(remainingNames) && locationsMatch(remainingLocations) &&
            checkedInputsMatch(true);
      };
      if (!frozenMatches(prepared->directory) || !committedMatches() || !publish(*prepared, &remainingNames, &remainingLocations)) {
        if (absentFile(prepared->directory) && frozenMatches(final) && committedMatches()) return prepared->id;
        return rollback();
      }
      return prepared->id;
    };
    auto result = [&]() -> std::variant<ca::GenerationId, End> {
      try { return execute(); }
      catch (const std::exception&) { return unknown("Cannot confirm removal settlement."); }
    }();
    auto* failure = std::get_if<End>(&result);
    if (!marked || (failure && failure->repair)) return result;

    const auto cleanup = [&] {
      if (ownsSelection) {
        const auto deadline = Clock::now() + std::chrono::minutes(1);
        if (!safeRelative(staging, fs::path(kPendingSelection)) || readText(selectionPath) != selection ||
            !writeText(selectionPath, {}) ||
            runBuild(config.cmake, staging, config.configuration, [&] { return Clock::now() >= deadline; }, true) ||
            !removalView()) return false;
        if (!failure && removalView() != remainingView) return false;
        if (failure && !removalRegistryMatches()) return false;
      }
      std::error_code ec;
      if (prepared && failure && !absentFile(prepared->directory)) {
        if (!realDirectory(prepared->directory) || !safeBuildTree(prepared->directory, [] { return false; })) return false;
        fs::remove_all(prepared->directory, ec);
        if (ec) return false;
      }
      if (!failure) {
        if (!safeRemovalUnit(backup, token)) return false;
        fs::remove_all(project / backup, ec);
        if (ec) return false;
      }
      bool retireRecord = false;
      if (ownsPending) {
        if (!realDirectory(project / pending)) return false;
        for (fs::directory_iterator at(project / pending, ec), end; !ec && at != end; at.increment(ec)) {
          const auto leaf = at->path().filename();
          if ((leaf != "selection" && leaf != "old-names" && leaf != "new-names" && leaf != "old-locations" &&
               leaf != "new-locations" && leaf != recoveryRecord.filename()) ||
              !exactTexturePath(project, pending / leaf)) return false;
        }
        if (ec) return false;
        for (const auto* leaf : {"selection", "old-names", "new-names", "old-locations", "new-locations"}) {
          const auto path = project / pending / leaf;
          if (!absentFile(path) && !DeleteFileW(path.c_str())) return false;
        }
        retireRecord = !absentFile(project / recoveryRecord);
      }
      // Keep the record through all required cleanup, including removal
      // of the markers. Bootstrap also treats a surviving record as a hold.
      if (!DeleteFileW((staging / kDirty).c_str()) || !DeleteFileW((project / kProjectDirty).c_str()) ||
          (retireRecord && !DeleteFileW((project / recoveryRecord).c_str()))) return false;
      if (ownsPending) RemoveDirectoryW((project / pending).c_str());  // Only an empty directory may remain.
      return true;
    };
    try {
      cleanupRepair = !cleanup();
    } catch (const std::exception&) {
      cleanupRepair = true;
    }
    if (cleanupRepair) {
      if (absentFile(staging / kDirty)) createMarker(staging / kDirty, {});
      if (absentFile(project / kProjectDirty))
        createMarker(project / kProjectDirty, utf8(staging) + "\n" + utf8(publication) + "\n");
      if (failure) return unknown("Cannot verify removal staging cleanup.");
    }
    return result;
  }

  std::variant<ca::GenerationId, End> updateTextureMetadata(const Work& work, bool& cleanupRepair) {
    const bool moving = work.move.has_value();
    const auto asset = work.job.asset;
    const auto expectedGeneration = moving ? work.move->expectedGeneration : work.rename->expectedGeneration;
    const auto& wanted = moving ? work.move->destinationLogicalPath : work.rename->displayName;
    const auto root = active.parent_path();
    const auto pending = project / ".pending" / as::toString(work.job.id.value);
    const auto destination = project / (moving ? kTextureLocations : kTextureNames);
    const auto journal = pending / (moving ? "move.json" : "rename.json");
    const auto readBytes = [&] { return moving ? readLocationBytes(project) : readNameBytes(project); };
    const auto& oldMetadata = moving ? activeLocations : activeNames;
    const auto oldBytes = nameBytes(oldMetadata);
    auto next = oldMetadata;
    const auto identity = as::toString(work.catalog->find(asset)->source);
    const auto token = textureName(identity);
    if (wanted == (moving ? fallbackPath(*token) : std::string(*token))) next.erase(asset);
    else next[asset] = wanted;
    const auto nextBytes = nameBytes(next);
    const auto nextNames = moving ? nullptr : &next;
    const auto nextLocations = moving ? &next : nullptr;
    std::optional<Prepared> prepared;
    bool owned = false, marked = false;
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const auto unknown = [&](std::string text) {
      return fail(Stage::Publication, Code::PublishFailed, std::move(text) +
          (moving ? " Keep the previous generation and inspect move.json offline." :
                    " Keep the previous generation and inspect the rename recovery record offline."), true);
    };
    const auto unit = textureDirectory(*token);
    const auto fragment = fs::path("registered") / unit / "catalog-source.txt";
    const auto canonical = as::serializeAssetCatalogSource(
        {{asset, as::CatalogAssetType::Texture, as::CatalogRoot::Content, utf8(unit / "source.png")}});
    const auto fragmentMatches = [&] {
      return exactTexturePath(project, fragment) && readMaterialText(project, fragment) == canonical;
    };
    const auto otherMetadataMatches = [&] {
      return moving ? namesMatch(activeNames) : locationsMatch(activeLocations);
    };
    const auto unchangedRegistry = [&] {
      return readRegistrations(project) == activeRegistered && registeredMaterials(project) == activeMaterials &&
          registeredTextures(project) == activeTextures && fragmentMatches() && otherMetadataMatches();
    };
    const auto oldState = [&]() -> std::optional<End> {
      const auto bytes = readBytes();
      const auto textures = registeredTextures(project);
      const auto imports = readRegistrations(project);
      const auto materials = registeredMaterials(project);
      if (!bytes || !textures || !imports || !materials)
        return unknown("Cannot verify the managed registry.");
      TextureNames observed;
      if (!bytes->empty()) {
        const auto value = json::parse(*bytes);
        const auto names = value.isOk() ? (moving ? parseLocations(value.value(), *textures) :
                                                 parseNames(value.value(), *textures)) : std::nullopt;
        if (!names || nameBytes(*names) != *bytes)
          return unknown("Cannot verify the committed texture metadata.");
        observed = std::move(*names);
      }
      if (moving && pathClaimed(observed, *textures, asset, wanted))
        return fail(Stage::Validation, Code::IdentityChanged, "Path conflict in the managed color texture registry.");
      if (*bytes != oldBytes || !otherMetadataMatches() || *textures != activeTextures || *imports != activeRegistered ||
          *materials != activeMaterials || config.generation != expectedGeneration)
        return fail(Stage::Validation, Code::IdentityChanged, "The active snapshot is stale; restart into the committed generation.");
      if (!moving && labelClaimed(activeNames, *textures, asset, wanted))
        return fail(Stage::Validation, Code::IdentityChanged, "Name conflict in the managed color texture registry.");
      if (!fragmentMatches())
        return fail(Stage::Validation, Code::IdentityChanged, "The original registration fragment changed.");
      return std::nullopt;
    };
    enum class NameExchange { Applied, Refused, TimedOut };
    const auto replace = [&](const fs::path& input, std::string_view prior, std::string_view value,
                             bool firstExchange) {
      if (readBytes() != prior ||
          (!value.empty() && readNameFile(project, input.lexically_relative(project), moving ? kLocationBytes : kNameBytes) != value))
        return NameExchange::Refused;
      if (firstExchange && interrupted()) return NameExchange::TimedOut;
      if (value.empty()) {
        if (!prior.empty()) DeleteFileW(destination.c_str());
      } else {
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (prior.empty() ? 0 : MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(input.c_str(), destination.c_str(), flags);
      }
      return readBytes() == value ? NameExchange::Applied : NameExchange::Refused;
    };
    const auto execute = [&]() -> std::variant<ca::GenerationId, End> {
      if (interrupted()) return timedOut(Stage::Launch);
      if (auto error = oldState()) return *error;
      const auto expectedHeader = inventoryHeader(inventory);
      const auto frozen = readManagedView(root, inventoryRelative, expectedHeader);
      const auto catalog = readCatalog(root);
      if (!frozen || *frozen != producers || !catalog || !matchesManagedCatalog(*catalog, *frozen) ||
          catalog->size() != work.catalog->size() || !retainsIdentity(*work.catalog, *catalog) ||
          std::ranges::any_of(catalog->records(), [&](const auto& row) {
            return work.catalog->find(row.guid)->dependencies != row.dependencies;
          }))
        return fail(Stage::Validation, Code::InvalidOutput, "Cannot verify active generation inputs.");
      const auto inputs = textureInputs(root, *frozen, *catalog);
      const auto materials = materialInputs(root, *frozen);
      if (!inputs || textureRegistrations(*frozen, *inputs) != activeTextures || !materials ||
          *materials != activeMaterials || !matchesFrozenMaterials(*catalog, *materials) ||
          !safeRelative(root, fs::path(kManifest)) || readJson(root / kManifest) !=
              manifest(owner, expectedGeneration, *frozen, inventoryRelative, *materials, *inputs, activeNames, activeLocations))
        return fail(Stage::Validation, Code::InvalidOutput, "Active generation differs from the frozen snapshot.");
      if (!createMarker(staging / kDirty, {}) ||
          !createMarker(project / kProjectDirty, utf8(staging) + "\n" + utf8(publication) + "\n"))
        return unknown("Cannot record both texture metadata workspace markers.");
      marked = true;
      Files files;
      auto result = prepare(*catalog, *frozen, interrupted, &prepared, nextNames, &files, nullptr, nextLocations);
      if (const auto* error = std::get_if<End>(&result)) return *error;
      prepared = std::get<Prepared>(std::move(result));
      const auto final = publication / as::toString(prepared->id.value);
      const auto matches = [&](const fs::path& directory) {
        if (!realDirectory(directory) || !safeRelative(directory, fs::path(kManifest)) ||
            readJson(directory / kManifest) != prepared->expectedManifest) return false;
        if (readManagedView(directory, inventoryRelative, expectedHeader) != producers) return false;
        return std::ranges::all_of(files, [&](const auto& file) {
          return safeRelative(root, file) && safeRelative(directory, file) &&
              sameOutput(root / file, directory / file, [] { return false; });
        });
      };
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.afterRenamePreparation) config.afterRenamePreparation(prepared->directory);
#endif
      std::lock_guard commit(publicationCommit);
      if (interrupted()) return fail(Stage::Publication, Code::TimedOut, "Texture metadata operation stopped before commit.");
      if (!matches(prepared->directory))
        return fail(Stage::Validation, Code::InvalidOutput, "Prepared generation was changed before commit.");
      if (auto error = oldState()) return *error;
      if (!absentFile(final)) return unknown("Cannot prove that the generation destination is vacant.");
      std::error_code ec;
      if (absentFile(pending.parent_path())) fs::create_directory(pending.parent_path(), ec);
      if (ec || !realDirectory(pending.parent_path()) || !fs::create_directory(pending, ec) || ec)
        return unknown("Cannot create owned texture metadata staging.");
      owned = true;
      auto proof = json::Value::object();
      proof.set("job", json::Value::string(as::toString(work.job.id.value)));
      proof.set("asset", json::Value::string(as::toString(asset)));
      proof.set("activeGeneration", json::Value::string(as::toString(expectedGeneration.value)));
      proof.set("generation", json::Value::string(as::toString(prepared->id.value)));
      proof.set("oldBytes", json::Value::string(oldBytes));
      proof.set("newBytes", json::Value::string(nextBytes));
      if ((!oldBytes.empty() && !createMarker(pending / "previous", oldBytes)) ||
          (!nextBytes.empty() && !createMarker(pending / "next", nextBytes)) ||
          !createMarker(journal, json::write(proof)))
        return fail(Stage::Publication, Code::PublishFailed, "Cannot save the texture metadata recovery record.");
      if (interrupted()) return fail(Stage::Publication, Code::TimedOut, "Texture metadata operation stopped before commit.");
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.beforeRenameExchange) config.beforeRenameExchange(pending, work.deadline);
#endif
      if (auto error = oldState()) return *error;
      const auto rollback = [&]() {
        if (!absentFile(final) || !unchangedRegistry() ||
            replace(pending / "previous", nextBytes, oldBytes, false) != NameExchange::Applied)
          return unknown("Cannot prove nonpublication and restore the prior texture metadata.");
        return fail(Stage::Publication, Code::PublishFailed, "The previous texture metadata was restored; retry explicitly.");
      };
      const auto exchange = replace(pending / "next", oldBytes, nextBytes, true);
      if (exchange == NameExchange::TimedOut)
        return fail(Stage::Publication, Code::TimedOut, "Texture metadata operation stopped before commit.");
      if (exchange != NameExchange::Applied) {
        if (absentFile(final) && unchangedRegistry() && readBytes() == oldBytes)
          return fail(Stage::Publication, Code::PublishFailed, "Cannot change the committed texture metadata.");
        return unknown("Cannot confirm the committed texture metadata.");
      }
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.beforeRenamePublication) config.beforeRenamePublication(prepared->directory, final);
#endif
      if (!unchangedRegistry() || !matches(prepared->directory) || readBytes() != nextBytes) {
        if (absentFile(prepared->directory) && matches(final) && readBytes() == nextBytes && unchangedRegistry())
          return prepared->id;
        return rollback();
      }
      if (!publish(*prepared, nextNames, nextLocations)) {
        if (absentFile(prepared->directory) && matches(final) && readBytes() == nextBytes && unchangedRegistry())
          return prepared->id;
        return rollback();
      }
      return prepared->id;
    };
    auto result = [&]() -> std::variant<ca::GenerationId, End> {
      try { return execute(); }
      catch (const std::exception&) { return unknown("Cannot confirm texture metadata settlement."); }
    }();
    const auto* error = std::get_if<End>(&result);
    if (!error || !error->repair) {
      std::error_code ec;
      const auto removeOwned = [&](const fs::path& path) {
        if (absentFile(path)) return true;
        if (!realDirectory(path) || !safeBuildTree(path, [] { return false; })) return false;
        fs::remove_all(path, ec);
        return !ec;
      };
      cleanupRepair = prepared && !removeOwned(prepared->directory);
      if (owned && !cleanupRepair && moving) {
        if (!realDirectory(pending)) cleanupRepair = true;
        else {
          for (fs::directory_iterator at(pending, ec), end; !ec && at != end; at.increment(ec)) {
            const auto leaf = at->path().filename();
            if ((leaf != "previous" && leaf != "next" && leaf != journal.filename()) ||
                !exactTexturePath(project, at->path().lexically_relative(project))) {
              cleanupRepair = true;
              break;
            }
          }
          cleanupRepair = cleanupRepair || static_cast<bool>(ec);
          for (const auto* leaf : {"previous", "next"}) {
            const auto path = pending / leaf;
            if (!cleanupRepair && !absentFile(path) && !DeleteFileW(path.c_str())) cleanupRepair = true;
          }
        }
      } else if (owned && !cleanupRepair) cleanupRepair = !removeOwned(pending);
      if (marked && !cleanupRepair) {
        fs::remove(staging / kDirty, ec);
        if (!ec) fs::remove(project / kProjectDirty, ec);
        cleanupRepair = static_cast<bool>(ec);
      }
      if (owned && moving && !cleanupRepair) {
        cleanupRepair = !absentFile(journal) && !DeleteFileW(journal.c_str());
        if (!cleanupRepair) RemoveDirectoryW(pending.c_str());
      }
      if (cleanupRepair && error) return unknown("Cannot confirm cleanup after a failed texture metadata operation.");
    }
    return result;
  }

  std::variant<ca::GenerationId, End> build(const Work& work) {
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    if (interrupted()) return timedOut(Stage::Launch);
    if (!metadataMatches())
      return fail(Stage::Validation, Code::IdentityChanged, "Managed display names differ from the active snapshot.", true);
    if (!project.empty()) {
      const auto textures = registeredTextures(project);
      if (!textures || *textures != activeTextures)
        return fail(Stage::Validation, Code::IdentityChanged, "Managed texture source differs from the active build.", !textures);
    }
    if (!project.empty() && registeredMaterials(project) != activeMaterials)
      return fail(Stage::Validation, Code::IdentityChanged, "Managed material source differs from the active build.");
    const fs::path marker = staging / kDirty;
    Handle marked(CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
    if (!marked) return fail(Stage::Launch, Code::LaunchFailed, "Cannot record staging ownership.", true);
    marked = Handle();
    if (!safeBuildTree(staging, interrupted))
      return interrupted() ? timedOut(Stage::Launch)
                           : fail(Stage::Launch, Code::LaunchFailed, "Staging contains an unsafe filesystem entry.", true);
    if (!safeRelative(staging, work.producer->stamp, true))
      return fail(Stage::Launch, Code::LaunchFailed, "Configured producer stamp is outside safe staging.", true);
    std::error_code ec;
    fs::remove(staging / work.producer->stamp, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
      return fail(Stage::Launch, Code::LaunchFailed, "Cannot invalidate the producer stamp.");
    if (interrupted()) return timedOut(Stage::Launch);
    if (!project.empty()) {
      if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true)) return *error;
    }
    if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted)) return *error;
    if (interrupted()) return timedOut(Stage::Validation);
    const auto catalog = readCatalog(staging);
    const auto after = readText(inventoryPath);
    const auto view = project.empty() ? std::optional{producers} : readManagedView(staging, inventoryRelative);
    if (!catalog || !after || !view || (project.empty() ? *after != inventory :
        inventoryHeader(*after) != inventoryHeader(inventory) || *view != producers ||
        materialInputs(staging, *view) != activeMaterials || registeredMaterials(project) != activeMaterials))
      return fail(Stage::Validation, Code::InvalidOutput, "Staging catalog or producer inventory is invalid.", true);
    if (work.catalog->size() != catalog->size() || !retainsIdentity(*work.catalog, *catalog))
      return fail(Stage::Validation, Code::IdentityChanged, "Rebuild changed the catalog identity set.");
    if (!project.empty()) {
      const auto inputs = textureInputs(staging, *view, *catalog);
      if (!inputs || textureRegistrations(*view, *inputs) != activeTextures || registeredTextures(project) != activeTextures)
        return fail(Stage::Validation, Code::IdentityChanged, "Rebuild changed the managed texture view.");
    }
    auto prepared = prepare(*catalog, *view, interrupted);
    if (const auto* error = std::get_if<End>(&prepared)) return *error;
    const auto& generation = std::get<Prepared>(prepared);
    std::lock_guard commit(publicationCommit);
    if (interrupted()) return timedOut(Stage::Publication);
    if (!publish(generation))
      return fail(Stage::Publication, Code::PublishFailed, "Cannot publish the generation directory.");
    return generation.id;
  }

  std::variant<ca::GenerationId, End> reimportColor(const Work& work, bool& cleanupRepair) {
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const std::string& name = *work.colorReplacement;
    const auto unit = textureDirectory(name);
    const auto pending = fs::path(".pending") / as::toString(work.job.id.value);
    const auto replacement = pending / "replace" / unit;
    const auto backup = pending / "previous";
    const auto registeredUnit = fs::path("registered") / unit;
    const auto input = fs::path("inbox") / fromUtf8(name) / "source.png";
    const auto publishedPath = [&](const Prepared& value) { return publication / as::toString(value.id.value); };
    const auto absent = [](const fs::path& path) {
      return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
    };
    const auto rename = [](const fs::path& from, const fs::path& to) {
      return MoveFileExW(from.c_str(), to.c_str(), 0) != 0;
    };
    const auto dirtyEnd = [](std::string message) {
      return fail(Stage::Publication, Code::PublishFailed, std::move(message) +
          " Keep the previous generation in use; inspect the recovery record offline before writing.", true);
    };

    bool marked = false, ownsPending = false;
    std::optional<Prepared> prepared;
    const auto execute = [&]() -> std::variant<ca::GenerationId, End> {
      if (interrupted()) return timedOut(Stage::Launch);
      const auto fragmentPath = registeredUnit / "catalog-source.txt";
      const auto before = registeredTextures(project);
      const auto imports = readRegistrations(project);
      const auto materials = registeredMaterials(project);
      if (!before || !imports || !materials || !metadataMatches())
        return fail(Stage::Validation, Code::InvalidOutput, "Cannot verify the managed registry; inspect it offline.", true);
      const auto old = before->find(name);
      const auto key = textureKey(name, TextureKind::Color);
      const auto canonical = as::serializeAssetCatalogSource(
          {{work.job.asset, as::CatalogAssetType::Texture, as::CatalogRoot::Content, utf8(unit / "source.png")}});
      if (*before != activeTextures || *imports != activeRegistered || *materials != activeMaterials ||
          old == before->end() || old->second.guid != work.job.asset || old->second.kind != TextureKind::Color ||
          !exactTexturePath(project, fragmentPath) || readMaterialText(project, fragmentPath) != canonical)
        return fail(Stage::Validation, Code::IdentityChanged, "Registration no longer matches the active generation.");

      if (!exactTexturePath(project, input))
        return fail(Stage::Validation, Code::InvalidOutput, "Invalid inbox PNG. Correct inbox/" + name + "/source.png and retry.");
      if (!createMarker(staging / kDirty, {}) ||
          !createMarker(project / kProjectDirty, utf8(staging) + "\n" + utf8(publication) + "\n"))
        return fail(Stage::Launch, Code::LaunchFailed, "Cannot record replacement workspace ownership.", true);
      marked = true;
      if (!safeBuildTree(staging, interrupted) || !safeRelative(staging, fs::path(kPendingSelection)))
        return interrupted() ? timedOut(Stage::Launch) :
            fail(Stage::Launch, Code::LaunchFailed, "Unsafe replacement staging.", true);

      std::error_code ec;
      const auto parent = project / ".pending";
      if (absent(parent)) fs::create_directory(parent, ec);
      if (ec || !realDirectory(parent) || !fs::create_directory(project / pending, ec) || ec)
        return fail(Stage::Launch, Code::LaunchFailed, "Cannot create private replacement staging.", true);
      ownsPending = true;
      if (!fs::create_directories(project / replacement, ec) || ec ||
          !exactTexturePath(project, input) ||
          !copyInbox(project / input, project / replacement / "source.png", interrupted, kPngBytes))
        return interrupted() ? timedOut(Stage::Pipeline) :
            fail(Stage::Validation, Code::InvalidOutput, "Cannot read a bounded ordinary inbox PNG. Correct the file and retry.");
      const auto digest = fileDigest(project, replacement / "source.png", kPngBytes);
      if (!digest || !writeText(project / replacement / "source.sha256", *digest + "\n") ||
          !writeText(project / replacement / "catalog-source.txt", canonical) ||
          !writeText(staging / kPendingSelection, utf8(replacement)))
        return fail(Stage::Launch, Code::LaunchFailed, "Cannot seal the frozen replacement PNG.");
      const TextureUnit newUnit{work.job.asset, *digest, TextureKind::Color};
      auto expected = *before;
      expected.at(name) = newUnit;
      if (!safeRelative(staging, work.producer->stamp, true))
        return fail(Stage::Launch, Code::LaunchFailed, "Configured texture stamp is outside safe staging.", true);
      fs::remove(staging / work.producer->stamp, ec);
      if (ec) return fail(Stage::Launch, Code::LaunchFailed, "Cannot invalidate the texture producer stamp.");
      if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true)) return *error;
      if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted)) {
        if (!error->repair) error->failure.diagnostic =
            "Cannot build the replacement PNG. Correct inbox/" + name + "/source.png and retry; see the build log.";
        return *error;
      }

      const auto sameCatalog = [&](const as::AssetCatalog& current) {
        return current.size() == work.catalog->size() && retainsIdentity(*work.catalog, current) &&
            std::ranges::all_of(work.catalog->records(), [&](const auto& record) {
              return std::ranges::is_permutation(record.dependencies, current.find(record.guid)->dependencies);
            });
      };
      const auto validView = [&](const fs::path& root, const as::AssetCatalog& catalog, const Producers& view) {
        const auto bytes = safeRelative(root, inventoryRelative) ? readText(root / inventoryRelative) : std::nullopt;
        return bytes && inventoryHeader(*bytes) == inventoryHeader(inventory) && view == producers &&
            sameCatalog(catalog) && matchesManagedCatalog(catalog, view) &&
            materialInputs(root, view) == materials && matchesFrozenMaterials(catalog, *materials);
      };
      const auto catalog = readCatalog(staging);
      const auto view = readManagedView(staging, inventoryRelative);
      const auto inputs = catalog && view ? textureInputs(staging, *view, *catalog) : std::nullopt;
      if (!catalog || !view || !inputs || !validView(staging, *catalog, *view) ||
          textureRegistrations(*view, *inputs) != expected)
        return fail(Stage::Validation, Code::InvalidOutput, "Staged replacement evidence or catalog identity differs.");

      auto result = prepare(*catalog, *view, interrupted, &prepared);
      if (const auto* error = std::get_if<End>(&result)) return *error;
      prepared = std::get<Prepared>(std::move(result));
      const auto expectedManifest = manifest(owner, prepared->id, *view, inventoryRelative, *materials, *inputs, activeNames, activeLocations);
      const auto matchesPrepared = [&](const fs::path& directory) {
        if (!realDirectory(directory)) return false;
        const auto frozen = readManagedView(directory, inventoryRelative);
        const auto frozenCatalog = readCatalog(directory);
        if (!frozen || !frozenCatalog || !validView(directory, *frozenCatalog, *frozen) ||
            !outputFiles(directory, *frozenCatalog, *frozen) || !safeRelative(directory, fs::path(kManifest))) return false;
        const auto copied = textureInputs(directory, *frozen, *frozenCatalog);
        return copied && readJson(directory / kManifest) == expectedManifest &&
            manifest(owner, prepared->id, *frozen, inventoryRelative, *materials, *copied, activeNames, activeLocations) == expectedManifest;
      };
      const auto unitMatches = [&](const fs::path& relative, const TextureUnit& unitInput) {
        return realDirectory(project / relative) && readTextureUnit(project, relative, name) == unitInput &&
            readMaterialText(project, relative / "catalog-source.txt") == canonical;
      };
      const auto unchangedRegistry = [&] {
        return readRegistrations(project) == imports && registeredMaterials(project) == materials;
      };
      const auto destination = publishedPath(*prepared);
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.afterTexturePreparation) config.afterTexturePreparation(prepared->directory, project / replacement / "catalog-source.txt");
#endif
      std::lock_guard commit(publicationCommit);
      if (interrupted()) return timedOut(Stage::Publication);
      if (!matchesPrepared(prepared->directory) || !unitMatches(replacement, newUnit))
        return fail(Stage::Validation, Code::InvalidOutput, "Prepared generation or sealed replacement changed.");
      if (!metadataMatches() || registeredTextures(project) != before || !unitMatches(registeredUnit, old->second) || !unchangedRegistry())
        return fail(Stage::Validation, Code::IdentityChanged, "The managed registry changed during reimport.", true);
      if (!absent(destination))
        return dirtyEnd("Cannot establish ownership of the prepared generation destination.");
      auto proof = json::Value::object();
      proof.set("job", json::Value::string(as::toString(work.job.id.value)));
      proof.set("generation", json::Value::string(as::toString(prepared->id.value)));
      proof.set("asset", json::Value::string(as::toString(work.job.asset)));
      proof.set("source", json::Value::string(key));
      proof.set("oldDigest", json::Value::string(old->second.digest));
      proof.set("newDigest", json::Value::string(*digest));
      if (!createMarker(project / pending / "replacement.json", json::write(proof)))
        return fail(Stage::Publication, Code::PublishFailed, "Cannot record the source exchange before publication.");
      if (interrupted()) return timedOut(Stage::Publication);
      const auto rollback = [&](std::string reason) {
        if (!absent(destination) || !unchangedRegistry() || !unitMatches(backup, old->second))
          return dirtyEnd(std::move(reason));
        if (!absent(project / registeredUnit)) {
          if (!unitMatches(registeredUnit, newUnit) || !absent(project / replacement) ||
              !rename(project / registeredUnit, project / replacement) || !absent(project / registeredUnit))
            return dirtyEnd(std::move(reason));
        }
        if (!rename(project / backup, project / registeredUnit) ||
            !unitMatches(registeredUnit, old->second) || registeredTextures(project) != before)
          return dirtyEnd(std::move(reason));
        return fail(Stage::Publication, Code::PublishFailed, std::move(reason) + " Previous registration restored; retry explicitly.");
      };
      if (!rename(project / registeredUnit, project / backup) || !unitMatches(backup, old->second) ||
          !absent(project / registeredUnit)) {
        if (absent(project / backup) && unitMatches(registeredUnit, old->second) && registeredTextures(project) == before)
          return fail(Stage::Publication, Code::PublishFailed, "Could not back up the registered unit; retry explicitly.");
        return dirtyEnd("Could not confirm the original unit backup.");
      }
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.afterTextureBackup) config.afterTextureBackup(project / replacement, project / registeredUnit);
#endif
      if (!rename(project / replacement, project / registeredUnit) ||
          !unitMatches(registeredUnit, newUnit) || registeredTextures(project) != expected || !unchangedRegistry())
        return rollback("Could not verify the replacement registration.");

      if (!metadataMatches() || !matchesPrepared(prepared->directory) || !unitMatches(registeredUnit, newUnit) ||
          registeredTextures(project) != expected || !unchangedRegistry())
        return rollback("The prepared generation or registry changed before publication.");
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.beforeTexturePublication) config.beforeTexturePublication(prepared->directory, destination);
#endif
      if (!publish(*prepared)) {
        if (absent(destination) && matchesPrepared(prepared->directory))
          return rollback("Could not publish the new generation.");
        if (!absent(prepared->directory) || !matchesPrepared(destination) ||
            registeredTextures(project) != expected || !unitMatches(registeredUnit, newUnit) || !unchangedRegistry() || !metadataMatches())
          return dirtyEnd("Cannot confirm the new generation publication.");
      }
      return prepared->id;
    };
    auto result = [&]() -> std::variant<ca::GenerationId, End> {
      try { return execute(); }
      catch (const std::exception&) { return dirtyEnd("Cannot verify replacement settlement."); }
    }();
    const auto* failed = std::get_if<End>(&result);
    if (marked && (!failed || !failed->repair)) {
      std::error_code ec;
      const auto removeOwned = [&](const fs::path& path) {
        if (absent(path)) return true;
        if (!realDirectory(path) || !safeBuildTree(path, [] { return false; })) return false;
        fs::remove_all(path, ec);
        return !ec;
      };
      cleanupRepair = (prepared && !removeOwned(prepared->directory)) ||
          (ownsPending && (!realDirectory(project / ".pending") || !removeOwned(project / pending))) ||
          !safeRelative(staging, fs::path(kPendingSelection)) || !writeText(staging / kPendingSelection, {});
      if (!cleanupRepair) {
        fs::remove(staging / kDirty, ec);
        if (!ec) fs::remove(project / kProjectDirty, ec);
        cleanupRepair = static_cast<bool>(ec);
      }
      if (cleanupRepair && failed) return dirtyEnd("Replacement failed and workspace cleanup could not be confirmed.");
    }
    return result;
  }

  struct ImportOutcome {
    std::optional<ca::GenerationId> generation;
    std::optional<ca::ImportFailure> failure;
    bool repair;
  };

  ImportOutcome buildImport(ImportWork& work) {
    using IS = ca::ImportStage;
    using IC = ca::ImportFailureCode;
    using RS = ca::RegistrationState;
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const auto failed = [&](IS stage, IC code, std::string message, bool repair = false,
                            std::optional<std::uint32_t> exit = std::nullopt) {
      if (interrupted() && stage != IS::Registration && stage != IS::Publication) {
        code = IC::TimedOut;
        repair = true;
      }
      return ImportOutcome{{}, ca::ImportFailure{stage, code, std::move(message), exit,
        work.registeredRoot ? RS::Committed : RS::NotCommitted, work.registeredRoot}, repair};
    };
    if (registeredMaterials(project) != activeMaterials)
      return failed(IS::Validation, IC::IdentityConflict, "Managed material source differs from the active build.");
    const auto initialTextures = registeredTextures(project);
    if (!initialTextures || *initialTextures != activeTextures || !metadataMatches())
      return failed(IS::Validation, IC::IdentityConflict, "Managed texture source differs from the active build.", !initialTextures);
    const fs::path name = fromUtf8("managed_" + work.job.name);
    const fs::path pendingParent = fs::path(".pending") / as::toString(work.job.id.value);
    const fs::path pending = pendingParent / name;
    const fs::path selection = staging / kPendingSelection;
    if (interrupted()) return failed(IS::Source, IC::TimedOut, "Import stopped before staging.");
    const auto ownerPaths = utf8(staging) + "\n" + utf8(publication) + "\n";
    if (!createMarker(staging / kDirty, {}) || !createMarker(project / kProjectDirty, ownerPaths))
      return failed(IS::Source, IC::InvalidSource, "Cannot mark workspace ownership.", true);
    if (!safeBuildTree(staging, interrupted) || !realDirectory(project / "registered") ||
        !safeBuildTree(project / "registered", interrupted) ||
        !safeRelative(staging, fs::path(kPendingSelection)))
      return failed(IS::Source, IC::InvalidSource, "Unsafe staging or registered input.", true);
    const auto diskRegistrations = readRegistrations(project);
    if (!diskRegistrations || *diskRegistrations != registered)
      return failed(IS::Source, IC::InvalidSource, "Registered inputs changed outside the worker.", true);
    std::error_code ec;
    as::AssetGuid root;
    if (work.registeredRoot) {
      root = *work.registeredRoot;
      if (!writeText(selection, {}))
        return failed(IS::Source, IC::InvalidSource, "Cannot clear pending selection.", true);
    } else {
      const auto inbox = fs::path("inbox") / fromUtf8(work.job.name + ".gltf");
      if (!safeRelative(project, inbox))
        return failed(IS::Source, IC::InvalidSource, "Inbox source is missing or unsafe.");
      if (!safeRelative(project, pending / "source.gltf", true) ||
          !fs::create_directories(project / pending, ec) || ec)
        return failed(IS::Source, IC::InvalidSource, "Cannot create private pending input.", true);
      if (!copyInbox(project / inbox, project / pending / "source.gltf", interrupted))
        return failed(IS::Source, IC::InvalidSource, "Cannot copy a bounded inbox source.");
      try {
        root = as::mintAssetGuids(1).front();
      } catch (const std::exception&) {
        return failed(IS::Registration, IC::RegisterFailed, "Cannot allocate the source GUID.");
      }
      const auto source = as::serializeAssetCatalogSource(
          {{root, as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, utf8(name / "source.gltf")}});
      if (!writeText(project / pending / "catalog-source.txt", source) || !writeText(selection, utf8(pending)))
        return failed(IS::Source, IC::InvalidSource, "Cannot write pending registration inputs.", true);
    }
    const auto key = managedSourceKey(work.job.name);
    // A previous refused attempt can leave the same producer's stamp in staging.
    const fs::path stamp = name / (utf8(name) + ".stamp");
    if (!safeRelative(staging, stamp, true))
      return failed(IS::Pipeline, IC::LaunchFailed, "Unsafe managed producer stamp.", true);
    fs::remove(staging / stamp, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
      return failed(IS::Pipeline, IC::LaunchFailed, "Cannot invalidate the managed stamp.", true);
    auto pipelineError = runBuild(config.cmake, staging, config.configuration, interrupted, true);
    if (!pipelineError) pipelineError = runBuild(config.cmake, staging, config.configuration, interrupted);
    if (pipelineError) {
      const auto code = pipelineError->failure.code == Code::TimedOut ? IC::TimedOut :
                        pipelineError->failure.code == Code::LaunchFailed ? IC::LaunchFailed : IC::BuildFailed;
      return failed(IS::Pipeline, code, pipelineError->failure.diagnostic, pipelineError->repair, pipelineError->failure.exitCode);
    }
    const auto catalog = readCatalog(staging);
    const auto view = readManagedView(staging, inventoryRelative);
    const auto inventoryAfter = readText(inventoryPath);
    if (!catalog || !view || !inventoryAfter || inventoryHeader(*inventoryAfter) != inventoryHeader(inventory) ||
        !matchesManagedCatalog(*catalog, *view))
      return failed(IS::Validation, IC::InvalidOutput, "Cannot validate managed staging output.", true);
    auto expected = activeRegistered;
    expected.emplace(work.job.name, root);
    const auto producer = view->find(key);
    const auto textures = textureInputs(staging, *view, *catalog);
    if (baseProducers(*view) != baseProducers(producers) || managedRegistrations(*view) != expected ||
        !textures || textureRegistrations(*view, *textures) != activeTextures ||
        materialInputs(staging, *view) != activeMaterials || registeredMaterials(project) != activeMaterials ||
        producer == view->end() || producer->second.type != as::CatalogAssetType::GltfImport ||
        !exactImportDelta(staging, *work.catalog, *catalog, producer->second, key))
      return failed(IS::Validation, IC::IdentityConflict, "Staged identity delta differs from this import.");
    auto prepared = prepare(*catalog, *view, interrupted);
    if (const auto* error = std::get_if<End>(&prepared)) {
      const auto stage = error->failure.stage == Stage::Validation ? IS::Validation : IS::Publication;
      const auto code = error->failure.code == Code::TimedOut ? IC::TimedOut :
                        stage == IS::Validation ? IC::InvalidOutput : IC::PublishFailed;
      return failed(stage, code, error->failure.diagnostic, error->repair);
    }
    const auto& generation = std::get<Prepared>(prepared);
    std::lock_guard commit(publicationCommit);
    if (interrupted())
      return failed(work.registeredRoot ? IS::Publication : IS::Registration, IC::TimedOut,
                    "Import stopped before the commit point.", true);
    if (!metadataMatches() || registeredTextures(project) != activeTextures)
      return failed(IS::Validation, IC::IdentityConflict, "Managed texture registry changed during import.", true);
    if (!work.registeredRoot) {
      if (!MoveFileExW((project / pending).c_str(), (project / "registered" / name).c_str(), 0)) {
        const auto disk = readRegistrations(project);
        if (disk && !disk->contains(work.job.name))
          return failed(IS::Registration, IC::RegisterFailed, "Cannot commit the source registration.");
        if (disk && disk->at(work.job.name) == root) {
          work.registeredRoot = root;
          return failed(IS::Registration, IC::RegisterFailed, "Registration needs workspace repair.", true);
        }
        auto outcome = failed(IS::Registration, IC::RegisterFailed, "Cannot verify source registration.", true);
        outcome.failure->registration = RS::Unknown;
        return outcome;
      }
      work.registeredRoot = root;
    }
    if (!publish(generation))
      return failed(IS::Publication, IC::PublishFailed, "Cannot publish the registered import generation.");
    return {generation.id, {}, false};
  }

  void settleImport(ImportWork& work) {
    auto result = buildImport(work);
    bool repair = result.repair;
    if (!repair) {
      std::error_code ec;
      const auto pending = project / ".pending" / as::toString(work.job.id.value);
      if (work.job.mode == ca::ImportMode::Register && realDirectory(pending)) {
        const auto unit = pending / fromUtf8("managed_" + work.job.name);
        if (realDirectory(unit)) {
          fs::remove(unit / "source.gltf", ec);
          if (!ec) fs::remove(unit / "catalog-source.txt", ec);
          if (!ec) fs::remove(unit, ec);
        }
        if (!ec) fs::remove(pending, ec);
      }
      repair = ec || !writeText(staging / kPendingSelection, {});
      if (!repair) {
        fs::remove(staging / kDirty, ec);
        if (!ec) fs::remove(project / kProjectDirty, ec);
        repair = static_cast<bool>(ec);
      }
    }
    auto& job = work.job;
    if (result.generation) {
      job.state = ca::RebuildState::Succeeded;
      job.rootGuid = work.registeredRoot;
      job.generation = result.generation;
    } else {
      job.state = ca::RebuildState::Failed;
      job.failure = std::move(result.failure);
    }
    std::lock_guard lock(mutex);
    if (work.registeredRoot) registered.insert_or_assign(job.name, *work.registeredRoot);
    dirty = repair;
    imported.push_front(std::move(job));
    if (imported.size() > 16) imported.pop_back();
    importing.reset();
  }

  void settleMaterial(MaterialWork& work) {
    using MS = ca::MaterialStage;
    using MC = ca::MaterialFailureCode;
    using RS = ca::RegistrationState;
    auto& job = work.job;
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const auto pending = fs::path(".pending") / as::toString(job.id.value);
    auto sourceState = RS::NotCommitted;
    std::optional<as::AssetGuid> saved;
    std::optional<Materials> before;
    bool repair = false;
    bool marked = false;
    bool ownsPending = false;
    bool sourceRenameStarted = false;
    fs::path candidate;
    std::optional<Prepared> prepared;
    const auto failJob = [&](MS stage, MC code, std::string diagnostic,
                             std::optional<std::uint32_t> exit = {}) {
      job.failure = ca::MaterialFailure{stage, code, std::move(diagnostic), exit, sourceState,
                                        sourceState == RS::Committed ? saved : std::nullopt};
    };
    const auto execute = [&] {
      before = registeredMaterials(project);
      const auto imports = readRegistrations(project);
      if (!before || !imports) {
        repair = true;
        return failJob(MS::Source, MC::InvalidSource, "Cannot verify the project registry.");
      }
      if (*imports != activeRegistered)
        return failJob(MS::Source, MC::ActivationRequired, "Activate the committed import changes first.");
      const auto registryTextures = registeredTextures(project);
      if (!registryTextures || *registryTextures != activeTextures || !metadataMatches()) {
        repair = !registryTextures;
        return failJob(MS::Source, MC::ActivationRequired, "Activate or repair the committed texture changes first.");
      }
      std::string name = job.target.name;
      ca::MaterialRecipe recipe;
      bool create = job.target.mode == ca::MaterialMode::Create || job.target.mode == ca::MaterialMode::Copy;
      const bool resume = job.target.mode == ca::MaterialMode::Resume;
      if (resume) {
        const auto found = std::ranges::find_if(*before, [&](const auto& row) {
          return row.second.guid == job.target.material;
        });
        if (found == before->end())
          return failJob(MS::Source, MC::NothingToResume, "No committed material matches this GUID.");
        name = found->first;
        saved = found->second.guid;
        const auto previous = activeMaterials.find(name);
        create = previous == activeMaterials.end();
        if (!create && (previous->second.guid != *saved || previous->second == found->second))
          return failJob(MS::Source, MC::NothingToResume, "The source is already active or has a different identity.");
        auto expected = activeMaterials;
        expected.insert_or_assign(name, found->second);
        if (expected != *before)
          return failJob(MS::Source, MC::ActivationRequired, "More than one committed change is ahead.");
        sourceState = RS::Committed;
        const auto parsed = as::parseMaterialSource(found->second.source);
        if (parsed.isErr()) return failJob(MS::Source, MC::InvalidSource, "The committed source is malformed.");
        const auto& input = parsed.value();
        recipe = {{input.textureAsset, {input.baseColorFactor[0], input.baseColorFactor[1],
                   input.baseColorFactor[2]}, input.metallicFactor, input.roughnessFactor, input.normalMapAsset,
                   input.emissiveTextureAsset, {input.emissiveFactor[0], input.emissiveFactor[1], input.emissiveFactor[2]},
                   input.metallicRoughnessMapAsset, input.occlusionMapAsset},
                  input.filter == as::MaterialSamplerFilter::Linear ? ca::MaterialFilter::Linear
                                                                     : ca::MaterialFilter::Nearest,
                  input.addressMode == as::MaterialSamplerAddressMode::Repeat ? ca::MaterialAddress::Repeat
                                                                              : ca::MaterialAddress::ClampToEdge};
        if (!ca::representsMaterialValues(recipe.values, work.version))
          return failJob(MS::Source, MC::InvalidRecipe, "Resume the committed recipe with a client that supports all of its material slots.");
        if (!ca::validMaterialRecipe(recipe) ||
            as::serializeMaterialSource(input) != as::serializeMaterialSource(material_authoring::source(recipe)))
          return failJob(MS::Source, MC::InvalidRecipe, "The committed source is outside the authoring subset.");
      } else {
        if (create && before->contains(name))
          return failJob(MS::Source, MC::NameConflict, "This material name is already registered.");
        if (*before != activeMaterials)
          return failJob(MS::Source, MC::ActivationRequired, "Activate or recover the committed materials first.");
        if (const auto* input = std::get_if<ca::CreateMaterial>(&work.request)) recipe.values = input->values;
        if (const auto* input = std::get_if<ca::CopyMaterial>(&work.request)) {
          auto read = material_authoring::read(*work.catalog, input->material, work.version);
          if (read.isErr()) return failJob(MS::Source, MC::InvalidRecipe, std::string(ca::toString(read.error())));
          recipe = read.value();
          recipe.values = input->values;
        }
        if (const auto* input = std::get_if<ca::EditMaterial>(&work.request)) {
          const auto found = std::ranges::find_if(*before, [&](const auto& row) {
            return row.second.guid == input->material;
          });
          if (found == before->end())
            return failJob(MS::Source, MC::NotEditable, "Only project-owned materials can be edited.");
          name = found->first;
          saved = input->material;
          auto read = material_authoring::read(*work.catalog, input->material, work.version);
          if (read.isErr()) return failJob(MS::Source, MC::InvalidRecipe, std::string(ca::toString(read.error())));
          recipe = read.value();
          if (input->activeGeneration != config.generation || input->expected != recipe.values)
            return failJob(MS::Source, MC::StaleEdit, "The active material differs from the original read.");
          if (input->values == recipe.values)
            return failJob(MS::Source, MC::Unchanged, "The replacement values are unchanged.");
          recipe.values = input->values;
        }
        if (create) saved = as::mintAssetGuids(1).front();
      }
      if (create && (work.catalog->find(*saved) || work.catalog->find(as::assetKey(*saved))))
        return failJob(MS::Validation, MC::IdentityConflict, "The material identity collides with an existing asset.");
      std::string_view slot;
      if (auto textures = material_authoring::textures(*work.catalog, recipe.values, &slot); textures.isErr())
        return failJob(MS::Source, MC::DependencyUnavailable,
                       std::string(slot) + ": " + std::string(ca::toString(textures.error())));
      const auto unit = materialDirectory(name);
      const auto expectedSource = resume ? before->at(name).source
          : as::serializeMaterialSource(material_authoring::source(recipe));
      auto expected = *before;
      expected.insert_or_assign(name, MaterialUnit{*saved, expectedSource});
      if (interrupted()) return failJob(MS::Source, MC::TimedOut, "Material job stopped before staging.");
      const auto ownerPaths = utf8(staging) + "\n" + utf8(publication) + "\n";
      if (!createMarker(staging / kDirty, {}) || !createMarker(project / kProjectDirty, ownerPaths)) {
        repair = true;
        return failJob(MS::Source, MC::InvalidSource, "Cannot mark workspace ownership.");
      }
      marked = true;
      if (!safeBuildTree(staging, interrupted) || !safeRelative(staging, fs::path(kPendingSelection))) {
        repair = true;
        return failJob(MS::Source, MC::InvalidSource, "Unsafe staging workspace.");
      }
      std::error_code ec;
      if (!resume) {
        candidate = pending / unit;
        const auto parent = project / ".pending";
        if ((!fs::exists(parent, ec) && !fs::create_directory(parent, ec)) || ec ||
            !realDirectory(parent) || !fs::create_directory(project / pending, ec) || ec)
          return failJob(MS::Source, MC::InvalidSource, "Cannot create private material staging.");
        ownsPending = true;
        const std::string fragment = "atlantis_asset_catalog_source_version: 1\nentry_count: 1\nasset: guid=" +
            as::toString(*saved) + " type=material root=content path=" + utf8(unit / kMaterialLeaf) + "\n";
        if (!fs::create_directory(project / candidate, ec) || ec ||
            !writeText(project / candidate / kMaterialLeaf, expectedSource) ||
            !writeText(project / candidate / "catalog-source.txt", fragment) ||
            !writeText(staging / kPendingSelection, utf8(candidate))) {
          return failJob(MS::Source, MC::InvalidSource, "Cannot stage the material source.");
        }
      }
      const auto stamp = fs::path("assets") / unit / "source.stamp";
      if (!safeRelative(staging, stamp, true)) {
        repair = true;
        return failJob(MS::Source, MC::InvalidSource, "Unsafe material producer stamp.");
      }
      fs::remove(staging / stamp, ec);
      if (ec && ec != std::errc::no_such_file_or_directory)
        return failJob(MS::Pipeline, MC::LaunchFailed, "Cannot invalidate the material producer stamp.");
      auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true);
      if (!error) error = runBuild(config.cmake, staging, config.configuration, interrupted);
      if (error) {
        repair = error->repair;
        const auto code = error->failure.code == Code::TimedOut ? MC::TimedOut :
            error->failure.code == Code::LaunchFailed ? MC::LaunchFailed : MC::BuildFailed;
        return failJob(MS::Pipeline, code, error->failure.diagnostic, error->failure.exitCode);
      }
      auto built = readCatalog(staging);
      const auto view = readManagedView(staging, inventoryRelative);
      const auto inventoryBytes = readText(inventoryPath);
      if (!built || !view || !inventoryBytes || inventoryHeader(*inventoryBytes) != inventoryHeader(inventory) ||
          baseProducers(*view) != baseProducers(producers) || managedRegistrations(*view) != activeRegistered ||
          materialInputs(staging, *view) != expected || !matchesManagedCatalog(*built, *view) ||
          !matchesFrozenMaterials(*built, expected))
        return failJob(MS::Validation, MC::InvalidOutput, "The staged material producer view is inconsistent.");
      const auto textureView = textureInputs(staging, *view, *built);
      if (!textureView || textureRegistrations(*view, *textureView) != activeTextures)
        return failJob(MS::Validation, MC::InvalidOutput, "Material save changed the managed texture view.");
      const auto expectedDelta = [&](const as::AssetCatalog& catalog) {
        return catalog.size() == work.catalog->size() + (create ? 1 : 0) && retainsIdentity(*work.catalog, catalog) &&
            std::ranges::all_of(work.catalog->records(), [&](const auto& record) {
              return record.guid == *saved ||
                  std::ranges::is_permutation(record.dependencies, catalog.find(record.guid)->dependencies);
            });
      };
      if (!expectedDelta(*built))
        return failJob(MS::Validation, MC::IdentityConflict, "The material build changed unrelated catalog identities.");
      const auto* builtRecord = built->find(*saved);
      auto cooked = material_authoring::read(*built, *saved);
      if (!builtRecord || as::toString(builtRecord->source) != materialKey(name) ||
          cooked.isErr() || cooked.value() != recipe)
        return failJob(MS::Validation, MC::InvalidOutput, "The cooked material differs from its intended recipe.");
      auto generation = prepare(*built, *view, interrupted, &prepared);
      if (const auto* prepareError = std::get_if<End>(&generation)) {
        repair = prepareError->repair;
        return failJob(prepareError->failure.stage == Stage::Validation ? MS::Validation : MS::Publication,
            prepareError->failure.code == Code::TimedOut ? MC::TimedOut :
            prepareError->failure.stage == Stage::Validation ? MC::InvalidOutput : MC::PublishFailed,
            prepareError->failure.diagnostic);
      }
      prepared = std::get<Prepared>(std::move(generation));
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.afterMaterialPreparation) config.afterMaterialPreparation(prepared->directory);
#endif
      const auto expectedManifest = manifest(owner, prepared->id, *view, inventoryRelative, expected, *textureView, activeNames, activeLocations);
      const auto matchesPrepared = [&](const fs::path& directory) {
        if (!realDirectory(directory) || !safeRelative(directory, inventoryRelative)) return false;
        const auto frozenInventory = readText(directory / inventoryRelative);
        if (!frozenInventory || inventoryHeader(*frozenInventory) != inventoryHeader(inventory)) return false;
        const auto frozen = readManagedView(directory, inventoryRelative);
        const auto catalog = frozen ? readCatalog(directory) : std::nullopt;
        if (!frozen || *frozen != *view || !catalog || !outputFiles(directory, *catalog, *frozen) ||
            materialInputs(directory, *frozen) != expected || !matchesManagedCatalog(*catalog, *frozen) ||
            !matchesFrozenMaterials(*catalog, expected) || !expectedDelta(*catalog)) return false;
        const auto copied = textureInputs(directory, *frozen, *catalog);
        return copied && safeRelative(directory, fs::path(kManifest)) &&
            readJson(directory / kManifest) == expectedManifest &&
            manifest(owner, prepared->id, *frozen, inventoryRelative, expected, *copied, activeNames, activeLocations) == expectedManifest;
      };

      std::lock_guard commit(publicationCommit);
      if (!matchesPrepared(prepared->directory))
        return failJob(MS::Validation, MC::InvalidOutput, "The prepared generation differs from the intended material source, catalog or texture evidence.");
      if (interrupted()) return failJob(MS::SourceCommit, MC::TimedOut, "Material job stopped before commit.");
      if (registeredMaterials(project) != before || readRegistrations(project) != imports ||
          registeredTextures(project) != activeTextures || !metadataMatches()) {
        repair = true;
        return failJob(MS::SourceCommit, MC::CommitFailed, "The project registry changed during the job.");
      }
      if (!resume) {
        const auto destination = project / "registered" / unit;
        const auto source = project / candidate;
        sourceRenameStarted = true;
        const bool committed = create ? MoveFileExW(source.c_str(), destination.c_str(), 0) != 0 :
            MoveFileExW((source / kMaterialLeaf).c_str(), (destination / kMaterialLeaf).c_str(),
                        MOVEFILE_REPLACE_EXISTING) != 0;
        const auto disk = registeredMaterials(project);
        if (disk != expected) {
          if (disk != before || committed) sourceState = RS::Unknown;
          repair = sourceState != RS::NotCommitted;
          return failJob(MS::SourceCommit, MC::CommitFailed, "Cannot verify the expected source commit.");
        }
        sourceState = RS::Committed;
      }
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.beforeMaterialPublication)
        config.beforeMaterialPublication(prepared->directory, publication / as::toString(prepared->id.value));
#endif
      if (!publish(*prepared)) {
        const auto destination = publication / as::toString(prepared->id.value);
        if (!realDirectory(prepared->directory) && metadataMatches() && matchesPrepared(destination)) {
          job.generation = prepared->id;
        } else {
          const DWORD attributes = GetFileAttributesW(destination.c_str());
          repair = attributes != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND ||
                   !realDirectory(prepared->directory);
          return failJob(MS::Publication, MC::PublishFailed, "Cannot publish the saved material generation.");
        }
      } else {
        job.generation = prepared->id;
      }
      job.material = saved;
    };
    try {
      execute();
    } catch (const std::exception&) {
      repair = marked;
      if (sourceRenameStarted && sourceState == RS::NotCommitted) sourceState = RS::Unknown;
      failJob(MS::Source, MC::InvalidSource, "Material job could not settle its workspace.");
    }
    if (marked && !repair) {
      std::error_code ec;
      if (ownsPending) {
        if (realDirectory(project / pending) && realDirectory(project / candidate)) {
          const auto sourcePath = candidate / kMaterialLeaf;
          const auto fragmentPath = candidate / "catalog-source.txt";
          if (!safeRelative(project, sourcePath, true) || !safeRelative(project, fragmentPath, true)) {
            ec = std::make_error_code(std::errc::permission_denied);
          } else {
            fs::remove(project / sourcePath, ec);
            if (!ec) fs::remove(project / fragmentPath, ec);
            if (!ec) fs::remove(project / candidate, ec);
          }
        }
        if (!ec && realDirectory(project / pending)) fs::remove(project / pending, ec);
        else if (!ec) ec = std::make_error_code(std::errc::permission_denied);
      }
      if (!ec && prepared && !job.generation && realDirectory(prepared->directory))
        fs::remove_all(prepared->directory, ec);
      repair = ec || !writeText(staging / kPendingSelection, {});
      if (!repair) {
        fs::remove(staging / kDirty, ec);
        if (!ec) fs::remove(project / kProjectDirty, ec);
        repair = static_cast<bool>(ec);
      }
    }
    const auto disk = registeredMaterials(project);
    job.state = job.generation ? ca::RebuildState::Succeeded : ca::RebuildState::Failed;
    std::lock_guard lock(mutex);
    if (disk) committedMaterials = *disk;
    dirty = repair || !disk;
    materialJobs.push_front(std::move(job));
    if (materialJobs.size() > 16) materialJobs.pop_back();
    materialWork.reset();
  }

  void settleTexture(TextureWork& work) {
    using IS = ca::ImportStage;
    using IC = ca::ImportFailureCode;
    using RS = ca::RegistrationState;
    auto& job = work.job;
    auto registration = RS::NotCommitted;
    std::optional<as::AssetGuid> guid;
    std::optional<Prepared> prepared;
    bool repair = false, marked = false, ownsPending = false, committing = false;
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    const auto unit = textureDirectory(job.name);
    const auto* leaf = textureLeaf(work.kind);
    const auto pending = fs::path(".pending") / as::toString(job.id.value);
    const auto candidate = pending / unit;
    const auto failJob = [&](IS stage, IC code, std::string reason, std::optional<std::uint32_t> exit = {}) {
      job.failure = ca::ImportFailure{stage, code, std::move(reason), exit, registration,
                                     registration == RS::Committed ? guid : std::nullopt};
    };
    const auto execute = [&] {
      const auto before = registeredTextures(project);
      const auto imports = readRegistrations(project);
      const auto materials = registeredMaterials(project);
      if (!before || !imports || !materials || !metadataMatches()) {
        repair = true;
        return failJob(IS::Source, IC::InvalidSource, "Cannot verify the managed registry and texture seals.");
      }
      auto expected = activeTextures;
      const auto found = before->find(job.name);
      const bool resume = job.mode == ca::ImportMode::Resume;
      if (resume && found != before->end() && found->second == work.resumeInput && !activeTextures.contains(job.name)) {
        guid = found->second.guid;
        expected.emplace(job.name, found->second);
        registration = RS::Committed;
      }
      if (work.kind == TextureKind::Color &&
          labelClaimed(activeNames, *before, resume && guid ? *guid : as::AssetGuid{}, job.name))
        return failJob(IS::Validation, IC::IdentityConflict, "Name conflict in the managed color texture registry.");
      if (work.kind == TextureKind::Color &&
          pathClaimed(activeLocations, *before, resume && guid ? *guid : as::AssetGuid{}, fallbackPath(job.name)))
        return failJob(IS::Validation, IC::IdentityConflict, "Path conflict in the managed color texture registry.");
      if ((resume && !guid) || (!resume && found != before->end()))
        return failJob(IS::Source, IC::InvalidSource, "The requested texture registration is no longer eligible.");
      if (*before != expected || *imports != activeRegistered || *materials != activeMaterials)
        return failJob(IS::Validation, IC::IdentityConflict, "Activate or repair other committed asset changes first.");
      if (interrupted()) return failJob(IS::Source, IC::TimedOut, "Texture import stopped before staging.");
      const auto ownerPaths = utf8(staging) + "\n" + utf8(publication) + "\n";
      if (!createMarker(staging / kDirty, {}) || !createMarker(project / kProjectDirty, ownerPaths)) {
        repair = true;
        return failJob(IS::Source, IC::InvalidSource, "Cannot mark texture workspace ownership.");
      }
      marked = true;
      if (!safeBuildTree(staging, interrupted) || !safeRelative(staging, fs::path(kPendingSelection))) {
        repair = true;
        return failJob(IS::Source, interrupted() ? IC::TimedOut : IC::InvalidSource, "Cannot access safe texture staging.");
      }
      std::error_code ec;
      const auto fragmentPath = candidate / "catalog-source.txt";
      std::string registrationFragment;
      if (!resume) {
        const auto inbox = fs::path("inbox") / fromUtf8(job.name) / "source.png";
        if (!exactTexturePath(project, inbox))
          return failJob(IS::Source, IC::InvalidSource, "The fixed inbox source.png is missing or unsafe.");
        const auto parent = project / ".pending";
        if ((!fs::exists(parent, ec) && !fs::create_directory(parent, ec)) || ec || !realDirectory(parent)) {
          repair = true;
          return failJob(IS::Source, IC::InvalidSource, "Cannot create private texture staging.");
        }
        if (!fs::create_directory(project / pending, ec) || ec) {
          repair = true;
          return failJob(IS::Source, IC::InvalidSource, "Cannot allocate a new private texture directory.");
        }
        ownsPending = true;
        if (!fs::create_directory(project / candidate, ec) || ec ||
            !copyInbox(project / inbox, project / candidate / leaf, interrupted, kPngBytes))
          return failJob(IS::Source, interrupted() ? IC::TimedOut : IC::InvalidSource, "Cannot copy the bounded texture.");
        const auto digest = fileDigest(project, candidate / leaf, kPngBytes);
        if (!digest) return failJob(IS::Source, IC::InvalidSource, "Cannot seal the copied texture source.");
        guid = newGuid();
        if (!guid) return failJob(IS::Registration, IC::RegisterFailed, "Cannot allocate the texture GUID.");
        expected.emplace(job.name, TextureUnit{*guid, *digest, work.kind});
        registrationFragment = as::serializeAssetCatalogSource(
            {{*guid, as::CatalogAssetType::Texture, as::CatalogRoot::Content, utf8(unit / leaf)}});
        if (!writeText(project / fragmentPath, registrationFragment) ||
            !writeText(project / candidate / "source.sha256", *digest + "\n") ||
            !writeText(staging / kPendingSelection, utf8(candidate)))
          return failJob(IS::Source, IC::InvalidSource, "Cannot write private texture inputs.");
      }
      if (work.catalog->find(*guid) || work.catalog->find(as::assetKey(*guid)))
        return failJob(IS::Validation, IC::IdentityConflict, "The texture identity collides with the active catalog.");
      auto stamp = fs::path("assets") / unit;
      stamp += ".stamp";
      if (!safeRelative(staging, stamp, true)) {
        repair = true;
        return failJob(IS::Pipeline, IC::LaunchFailed, "Unsafe texture producer stamp.");
      }
      fs::remove(staging / stamp, ec);
      if (ec) return failJob(IS::Pipeline, IC::LaunchFailed, "Cannot invalidate the texture producer stamp.");
      auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true);
      if (!error) error = runBuild(config.cmake, staging, config.configuration, interrupted);
      if (error) {
        repair = error->repair;
        const auto code = error->failure.code == Code::TimedOut ? IC::TimedOut :
            error->failure.code == Code::LaunchFailed ? IC::LaunchFailed : IC::BuildFailed;
        return failJob(IS::Pipeline, code, error->failure.diagnostic, error->failure.exitCode);
      }
      const auto built = readCatalog(staging);
      const auto view = readManagedView(staging, inventoryRelative);
      const auto inventoryBytes = readText(inventoryPath);
      const auto inputs = built && view ? textureInputs(staging, *view, *built) : std::nullopt;
      if (!built || !view || !inputs || !inventoryBytes || inventoryHeader(*inventoryBytes) != inventoryHeader(inventory) ||
          baseProducers(*view) != baseProducers(producers) || managedRegistrations(*view) != *imports ||
          materialInputs(staging, *view) != *materials || !matchesManagedCatalog(*built, *view) ||
          !matchesFrozenMaterials(*built, *materials) || textureRegistrations(*view, *inputs) != expected)
        return failJob(IS::Validation, IC::InvalidOutput, "Staged texture producer evidence is inconsistent.");
      const auto expectedDelta = [&](const as::AssetCatalog& catalog) {
        return catalog.size() == work.catalog->size() + 1 && retainsIdentity(*work.catalog, catalog) &&
            std::ranges::all_of(work.catalog->records(), [&](const auto& record) {
              return std::ranges::is_permutation(record.dependencies, catalog.find(record.guid)->dependencies);
            });
      };
      if (!expectedDelta(*built))
        return failJob(IS::Validation, IC::IdentityConflict, "Texture import changed an unrelated catalog record.");
      auto result = prepare(*built, *view, interrupted, &prepared);
      if (const auto* preparation = std::get_if<End>(&result)) {
        repair = preparation->repair;
        const bool validating = preparation->failure.stage == Stage::Validation;
        return failJob(validating ? IS::Validation : IS::Publication,
            preparation->failure.code == Code::TimedOut ? IC::TimedOut :
            validating ? IC::InvalidOutput : IC::PublishFailed, preparation->failure.diagnostic);
      }
      prepared = std::get<Prepared>(std::move(result));
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.afterTexturePreparation) config.afterTexturePreparation(prepared->directory, project / fragmentPath);
#endif
      const auto expectedManifest = manifest(owner, prepared->id, *view, inventoryRelative, *materials, *inputs, activeNames, activeLocations);
      const auto matchesPrepared = [&](const fs::path& directory) {
        if (!realDirectory(directory) || !safeRelative(directory, inventoryRelative)) return false;
        const auto frozenInventory = readText(directory / inventoryRelative);
        if (!frozenInventory || inventoryHeader(*frozenInventory) != inventoryHeader(inventory)) return false;
        const auto frozen = readManagedView(directory, inventoryRelative);
        const auto catalog = readCatalog(directory);
        if (!frozen || *frozen != *view || !catalog || !outputFiles(directory, *catalog, *frozen) ||
            !expectedDelta(*catalog) || !matchesManagedCatalog(*catalog, *frozen) ||
            materialInputs(directory, *frozen) != *materials || !matchesFrozenMaterials(*catalog, *materials)) return false;
        const auto copied = textureInputs(directory, *frozen, *catalog);
        return copied && safeRelative(directory, fs::path(kManifest)) &&
            readJson(directory / kManifest) == expectedManifest &&
            manifest(owner, prepared->id, *frozen, inventoryRelative, *materials, *copied, activeNames, activeLocations) == expectedManifest;
      };
      std::lock_guard commit(publicationCommit);
      const bool preparedMatches = matchesPrepared(prepared->directory);
      const auto current = registeredTextures(project);
      if (resume && (!current || !current->contains(job.name) || current->at(job.name) != expected.at(job.name)))
        registration = RS::Unknown;
      if (!metadataMatches() || current != before || readRegistrations(project) != imports ||
          registeredMaterials(project) != materials ||
          (!resume && (readTextureUnit(project, candidate, job.name) != expected.at(job.name) ||
                       readMaterialText(project, fragmentPath) != registrationFragment))) {
        repair = true;
        return failJob(IS::Registration, IC::RegisterFailed, "The registered or private texture input changed.");
      }
      if (interrupted()) return failJob(IS::Registration, IC::TimedOut, "Texture import stopped before commit.");
      if (!preparedMatches)
        return failJob(IS::Validation, IC::InvalidOutput, "The prepared texture generation differs from the validated build.");
      if (!resume) {
        committing = true;
        const bool moved = MoveFileExW((project / candidate).c_str(), (project / "registered" / unit).c_str(), 0) != 0;
        const auto disk = registeredTextures(project);
        if (disk != expected) {
          if (moved || disk != before) {
            repair = true;
            registration = RS::Unknown;
          }
          return failJob(IS::Registration, IC::RegisterFailed, "Cannot verify the texture registration.");
        }
        registration = RS::Committed;
      }
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
      if (config.beforeTexturePublication)
        config.beforeTexturePublication(prepared->directory, publication / as::toString(prepared->id.value));
#endif
      if (!publish(*prepared)) {
        const auto destination = publication / as::toString(prepared->id.value);
        const auto attributes = GetFileAttributesW(destination.c_str());
        const auto missing = attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
        if (realDirectory(prepared->directory) || !metadataMatches() || !matchesPrepared(destination)) {
          repair = !missing || !realDirectory(prepared->directory);
          return failJob(IS::Publication, IC::PublishFailed, "Cannot confirm texture generation publication.");
        }
      }
      job.generation = prepared->id;
      job.rootGuid = guid;
    };
    try {
      execute();
    } catch (const std::exception&) {
      repair = marked;
      if (committing && registration == RS::NotCommitted) registration = RS::Unknown;
      failJob(IS::Source, IC::InvalidSource, "Cannot settle the texture workspace.");
    }
    if (marked && !repair) {
      std::error_code ec;
      if (ownsPending) {
        if (realDirectory(project / candidate)) {
          for (const auto* name : {leaf, "source.sha256", "catalog-source.txt"}) {
            const auto relative = candidate / name;
            if (!safeRelative(project, relative, true)) {
              ec = std::make_error_code(std::errc::permission_denied);
              break;
            }
            fs::remove(project / relative, ec);
            if (ec) break;
          }
          if (!ec) fs::remove(project / candidate, ec);
        }
        if (!ec) fs::remove(project / pending, ec);
      }
      if (!ec && prepared && !job.generation) {
        if (realDirectory(prepared->directory) && safeBuildTree(prepared->directory, [] { return false; }))
          fs::remove_all(prepared->directory, ec);
        else
          ec = std::make_error_code(std::errc::permission_denied);
      }
      repair = ec || !writeText(staging / kPendingSelection, {});
      if (!repair) {
        fs::remove(staging / kDirty, ec);
        if (!ec) fs::remove(project / kProjectDirty, ec);
        repair = static_cast<bool>(ec);
      }
    }
    const auto disk = registeredTextures(project);
    job.state = job.generation ? ca::RebuildState::Succeeded : ca::RebuildState::Failed;
    std::lock_guard lock(mutex);
    dirty = dirty || repair || !disk;
    if (disk) committedTextures = *disk;
    textureRegistryChanged = committedTextures != activeTextures;
    textureJobs.emplace_front(work.kind, std::move(job));
    if (textureJobs.size() > 16) textureJobs.pop_back();
    textureWork.reset();
  }

  void run() {
    for (;;) {
      std::optional<Work> work;
      std::optional<ImportWork> import;
      std::optional<MaterialWork> material;
      std::optional<TextureWork> texture;
      std::optional<RemovalRead> removal;
      {
        std::unique_lock lock(mutex);
        const auto checking = [&] { return removalCheck && removalCheck->check.state == ca::TextureRemovalState::Checking; };
        changed.wait(lock, [&] { return stopping || running || importing || materialWork || textureWork || checking(); });
        if (stopping && !running && !importing && !materialWork && !textureWork && !checking()) return;
        work = running;
        import = importing;
        material = materialWork;
        texture = textureWork;
        if (checking()) removal = removalCheck;
      }
      if (removal) {
        settleRemovalCheck(std::move(*removal));
        continue;
      }
      if (texture) {
        settleTexture(*texture);
        continue;
      }
      if (material) {
        settleMaterial(*material);
        continue;
      }
      if (import) {
        settleImport(*import);
        continue;
      }
      bool cleanupRepair = false;
      auto result = work->removal ? removeColor(*work, cleanupRepair) : (work->rename || work->move) ? updateTextureMetadata(*work, cleanupRepair) :
          work->colorReplacement ? reimportColor(*work, cleanupRepair) : build(*work);
      const auto disk = work->colorReplacement ? registeredTextures(project) : std::nullopt;
      const auto names = (work->rename || work->removal) ? readNameBytes(project) : std::nullopt;
      std::optional<TextureNames> settledNames;
      if (names) {
        if (names->empty()) settledNames.emplace();
        else if (const auto parsed = json::parse(*names); parsed.isOk()) {
          settledNames = parseNames(parsed.value(), disk ? *disk : activeTextures);
          if (settledNames && nameBytes(*settledNames) != *names) settledNames.reset();
        }
      }
      const auto settledLocations = work->move || work->removal
          ? readLocations(project, disk ? *disk : activeTextures) : std::nullopt;
      auto& job = work->job;
      const auto* failure = std::get_if<End>(&result);
      bool repair = cleanupRepair || (failure && failure->repair) || (work->colorReplacement && !disk) ||
          ((work->rename || work->removal) && !settledNames) ||
          ((work->move || work->removal) && !settledLocations);
      if (!repair && !work->colorReplacement && !work->rename && !work->move) {
        std::error_code ec;
        fs::remove(staging / kDirty, ec);
        repair = static_cast<bool>(ec);
      }
      if (failure) {
        job.state = ca::RebuildState::Failed;
        job.failure = failure->failure;
      } else {
        job.state = ca::RebuildState::Succeeded;
        job.generation = std::get<ca::GenerationId>(result);
      }
      std::lock_guard lock(mutex);
      dirty = repair;
      if (disk) {
        committedTextures = *disk;
        textureRegistryChanged = committedTextures != activeTextures;
      }
      if (settledNames) committedNames = std::move(*settledNames);
      if (settledLocations) committedLocations = *settledLocations;
      completed.push_front(std::move(job));
      if (completed.size() > 16) completed.pop_back();
      running.reset();
    }
  }
};

AssetRebuildHost::AssetRebuildHost(std::unique_ptr<State> state) : state_(std::move(state)) {}
AssetRebuildHost::~AssetRebuildHost() = default;
void AssetRebuildHost::stop() { state_->stop(); }

Result<std::unique_ptr<AssetRebuildHost>, std::string> AssetRebuildHost::open(Config config) {
  using R = Result<std::unique_ptr<AssetRebuildHost>, std::string>;
  auto state = std::make_unique<State>();
  state->config = std::move(config);
  const auto profile = readJson(state->config.profile);
  if (!profile) return R::Err("Cannot read the asset build profile.");
  const auto stage = canonicalDirectory(profile->find("stagingBuild"));
  const auto publication = canonicalDirectory(profile->find("publicationRoot"));
  std::error_code ec;
  const auto seed = fs::canonical(state->config.seedCatalog.parent_path(), ec);
  if (!stage || !publication || ec || overlap(seed, *stage) || overlap(seed, *publication) || overlap(*stage, *publication))
    return R::Err("The seed, staging and publication trees must be existing disjoint directories.");
  state->staging = *stage;
  state->publication = *publication;
  const auto inventory = fs::path("assets") / ("producers-" + state->config.configuration + ".txt");
  state->inventoryRelative = inventory;
  state->inventoryPath = *stage / inventory;
  bool readDuringRepair = false;
  if (const auto* optIn = profile->find("importProject")) {
    const auto project = canonicalDirectory(optIn);
    if (!project || !safeProjectRoot(fromUtf8(optIn->asString()), *project) ||
        overlap(*project, seed) || overlap(*project, *stage) || overlap(*project, *publication))
      return R::Err("Import project must be an existing disjoint directory.");
    state->project = *project;
    const auto markerExists = [](const fs::path& root, std::string_view name) {
      const auto attributes = GetFileAttributesW((root / name).c_str());
      return attributes != INVALID_FILE_ATTRIBUTES;
    };
    readDuringRepair = state->config.generation &&
        (markerExists(*stage, kDirty) || markerExists(*project, kProjectDirty));
    for (const auto& tree : {seed, *stage}) {
      if (tree == *stage && readDuringRepair) continue;
      if (!safeRelative(tree, fs::path(kManagedProject)))
        return R::Err("Both builds must be configured with the import project.");
      const auto configured = readText(tree / kManagedProject);
      if (!configured || !fs::equivalent(fromUtf8(*configured), *project, ec) || ec)
        return R::Err("Both builds must use the configured import project.");
    }
  }
  if (!safeRelative(seed, inventory))
    return R::Err("The configured seed producer inventory is missing or unsafe.");
  const auto seedInventory = readText(seed / inventory);
  const auto stageInventory = safeRelative(*stage, inventory) ? readText(state->inventoryPath) : std::nullopt;
  if (!seedInventory || (!stageInventory && !readDuringRepair))
    return R::Err("Cannot read the configured producer inventories.");
  auto producers = stageInventory ? readProducers(*stageInventory) : std::nullopt;
  if (state->project.empty()) {
    if (!producers || *seedInventory != *stageInventory || !managedRegistrations(*producers).empty() ||
        baseProducers(*producers) != *producers)
      return R::Err("Staging must use the same checkout, generator, configuration and asset declarations.");
  } else {
    const auto seedHeader = columns(std::string(inventoryHeader(*seedInventory)));
    if (seedHeader.size() < 2 || overlap(state->project, fromUtf8(seedHeader[1])))
      return R::Err("Import project must be disjoint from the checkout.");
    auto seedView = readManagedView(seed, inventory);
    if (!seedView || (!readDuringRepair && (!producers ||
        inventoryHeader(*seedInventory) != inventoryHeader(*stageInventory) ||
        baseProducers(*seedView) != baseProducers(*producers))))
      return R::Err("Managed staging must match the seed's base producers and build configuration.");
    auto materials = materialInputs(seed, *seedView);
    if (!materials) return R::Err("Seed managed material sources are missing or invalid.");
    state->activeMaterials = std::move(*materials);
    producers = std::move(seedView);
  }
  state->inventory = *seedInventory;
  state->producers = std::move(*producers);
  const std::string buildIdentity = state->inventory.substr(0, state->inventory.find('\n'));
  auto binding = json::Value::object();
  binding.set("seed", json::Value::string(utf8(seed)));
  binding.set("staging", json::Value::string(utf8(*stage)));
  binding.set("publication", json::Value::string(utf8(*publication)));
  binding.set("configuration", json::Value::string(state->config.configuration));
  binding.set("build", json::Value::string(buildIdentity));
  if (!state->project.empty()) binding.set("importProject", json::Value::string(utf8(state->project)));
  state->owner = json::write(binding);
  const fs::path ownerPath = *publication / kOwner;
  if (!safeRelative(*publication, fs::path(kOwner), true))
    return R::Err("Unsafe publication root profile binding.");
  Handle owner(CreateFileW(ownerPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
  if (owner) {
    DWORD written = 0;
    if (!WriteFile(owner.get(), state->owner.data(), static_cast<DWORD>(state->owner.size()), &written, nullptr) ||
        written != state->owner.size() || !FlushFileBuffers(owner.get()))
      return R::Err("Cannot bind the publication root.");
  } else {
    const auto recorded = readJson(ownerPath);
    const auto oldSeed = recorded ? canonicalDirectory(recorded->find("seed")) : std::nullopt;
    const auto oldStage = recorded ? canonicalDirectory(recorded->find("staging")) : std::nullopt;
    const auto oldPublication = recorded ? canonicalDirectory(recorded->find("publication")) : std::nullopt;
    const auto* configuration = recorded ? recorded->find("configuration") : nullptr;
    const auto* build = recorded ? recorded->find("build") : nullptr;
    const auto* oldProjectValue = recorded ? recorded->find("importProject") : nullptr;
    const auto oldProject = canonicalDirectory(oldProjectValue);
    if (state->project.empty() ? oldProjectValue != nullptr :
        !oldProject || !fs::equivalent(state->project, *oldProject, ec) || ec)
      return R::Err("Publication root belongs to a different import project.");
    if (!oldSeed || !oldStage || !oldPublication || !fs::equivalent(seed, *oldSeed, ec) || ec ||
        !fs::equivalent(*stage, *oldStage, ec) || ec || !fs::equivalent(*publication, *oldPublication, ec) || ec ||
        !configuration || !configuration->isString() ||
        configuration->asString() != state->config.configuration || !build || !build->isString() ||
        build->asString() != buildIdentity)
      return R::Err("Publication root belongs to a different profile.");
    state->owner = json::write(*recorded);
  }
  state->active = seed / kCatalog;
  if (!state->project.empty() && !state->config.generation) {
    const auto catalog = readCatalog(seed);
    const auto textures = catalog ? textureInputs(seed, state->producers, *catalog) : std::nullopt;
    if (!textures) return R::Err("Seed managed texture evidence is missing or invalid.");
    state->activeTextures = textureRegistrations(state->producers, *textures);
  }
  if (const auto generation = state->config.generation) {
    const fs::path selected = *publication / as::toString(generation->value);
    if (!realDirectory(selected) || !safeRelative(selected, fs::path(kManifest)))
      return R::Err("Unknown, incomplete or foreign asset generation.");
    const auto selectedManifest = readJson(selected / kManifest);
    if (!selectedManifest || (state->project.empty() &&
        *selectedManifest != manifest(state->owner, *generation, state->producers)))
      return R::Err("Unknown, incomplete or foreign asset generation.");
    const auto catalog = readCatalog(selected);
    if (!catalog) return R::Err("Incomplete asset generation files.");
    TextureInputs selectedTextures;
    if (!state->project.empty()) {
      auto frozen = readManagedView(selected, inventory, inventoryHeader(state->inventory));
      if (!frozen || baseProducers(*frozen) != baseProducers(state->producers) ||
          !matchesManagedCatalog(*catalog, *frozen))
        return R::Err("Invalid managed generation metadata.");
      auto materials = materialInputs(selected, *frozen);
      if (!materials || !matchesFrozenMaterials(*catalog, *materials))
        return R::Err("Managed generation material sources are missing or invalid.");
      auto textures = textureInputs(selected, *frozen, *catalog);
      if (!textures) return R::Err("Managed generation texture evidence is missing or invalid.");
      state->activeTextures = textureRegistrations(*frozen, *textures);
      if (const auto* names = selectedManifest->find("textureDisplayNames")) {
        const auto parsed = parseNames(*names, state->activeTextures);
        if (!parsed) return R::Err("Invalid frozen texture display names.");
        state->activeNames = *parsed;
      }
      if (const auto* locations = selectedManifest->find("textureLocations")) {
        const auto parsed = parseLocations(*locations, state->activeTextures);
        if (!parsed) return R::Err("Invalid frozen texture locations.");
        state->activeLocations = *parsed;
      }
      selectedTextures = std::move(*textures);
      state->activeMaterials = std::move(*materials);
      state->producers = std::move(*frozen);
    }
    const auto managedInventory = state->project.empty() ? fs::path{} : inventory;
    if (*selectedManifest != manifest(state->owner, *generation, state->producers, managedInventory,
                                     state->activeMaterials, selectedTextures, state->activeNames, state->activeLocations))
      return R::Err("Unknown, incomplete or foreign asset generation.");
    if (!outputFiles(selected, *catalog, state->producers))
      return R::Err("Incomplete asset generation files.");
    state->active = selected / kCatalog;
  }
  const fs::path lock = *stage / ".atlantis-rebuild-lock";
  if (!safeRelative(*stage, lock.filename(), true)) return R::Err("Unsafe workspace writer lock path.");
  state->writer = Handle(CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr));
  if (!state->writer && GetLastError() != ERROR_SHARING_VIOLATION) return R::Err("Cannot open the workspace writer lock.");
  if (!state->project.empty()) {
    state->activeRegistered = managedRegistrations(state->producers);
    if (!state->config.generation) {
      const auto catalog = readCatalog(seed);
      if (!catalog || !matchesManagedCatalog(*catalog, state->producers) ||
          !matchesFrozenMaterials(*catalog, state->activeMaterials))
        return R::Err("Seed catalog does not match its frozen managed producer view.");
    }
    if (state->writer) {
      const fs::path projectLock = ".atlantis-import-lock";
      if (!safeRelative(state->project, projectLock, true)) return R::Err("Unsafe import project writer lock.");
      state->projectWriter = Handle(CreateFileW((state->project / projectLock).c_str(),
          GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr));
      if (!state->projectWriter && GetLastError() != ERROR_SHARING_VIOLATION)
        return R::Err("Cannot open import project writer lock.");
    }
  }
  if (state->writer && (state->project.empty() || state->projectWriter)) {
    state->dirty = readDuringRepair || !safeRelative(*stage, fs::path(kDirty), true) ||
        fs::exists(*stage / kDirty, ec) || static_cast<bool>(ec);
    if (!state->project.empty()) {
      auto registered = readRegistrations(state->project);
      auto materials = registeredMaterials(state->project);
      auto textures = registeredTextures(state->project);
      const auto pending = safeRelative(*stage, fs::path(kPendingSelection))
          ? readText(*stage / kPendingSelection) : std::nullopt;
      state->dirty = state->dirty || !registered || !materials || !textures || hasPendingTextureRecovery(state->project) ||
          !safeRelative(state->project, fs::path(kProjectDirty), true) ||
          fs::exists(state->project / kProjectDirty, ec) || ec || !pending || !pending->empty();
      if (registered) state->registered = std::move(*registered);
      if (materials) state->committedMaterials = std::move(*materials);
      const auto display = readNameBytes(state->project);
      if (!display) state->dirty = true;
      else if (!display->empty()) {
        const auto parsed = json::parse(*display);
        const auto names = parsed.isOk() && textures ? parseNames(parsed.value(), *textures) : std::nullopt;
        if (!names || nameBytes(*names) != *display) state->dirty = true;
        else state->committedNames = *names;
      }
      const auto locations = textures ? readLocations(state->project, *textures) : std::nullopt;
      if (!locations) state->dirty = true;
      else state->committedLocations = *locations;
      if (textures) state->committedTextures = std::move(*textures);
      state->textureRegistryChanged = state->committedTextures != state->activeTextures;
    }
    auto* workState = state.get();
    try {
      state->worker = std::thread([workState] { workState->run(); });
    } catch (const std::system_error&) {
      return R::Err("Cannot start the asset rebuild worker.");
    }
  }
  return R::Ok(std::unique_ptr<AssetRebuildHost>(new AssetRebuildHost(std::move(state))));
}
const fs::path& AssetRebuildHost::catalogPath() const noexcept { return state_->active; }
fs::path AssetRebuildHost::importedSource(as::AssetGuid root) const {
  for (const auto& [key, producer] : state_->producers)
    if (producer.root == root && !producer.generated.empty()) return state_->active.parent_path() / producer.generated;
  return {};
}
std::optional<ca::RebuildError> AssetRebuildHost::admissionError() const {
  std::lock_guard lock(state_->mutex);
  return state_->admissionLocked();
}
ca::RebuildStartResult AssetRebuildHost::start(as::AssetGuid asset, std::shared_ptr<const as::AssetCatalog> catalog) {
  std::lock_guard lock(state_->mutex);
  using R = ca::RebuildStartResult;
  if (const auto error = state_->admissionLocked()) return R::Err(*error);
  if (!catalog) return R::Err(ca::RebuildError::CatalogUnavailable);
  const auto* record = catalog->find(asset);
  if (!record) return R::Err(ca::RebuildError::UnknownAsset);
  const auto producer = state_->producers.find(as::toString({record->source.root, record->source.path, {}}));
  const bool derived = !record->source.subKey.empty();
  if (producer == state_->producers.end() ||
      (derived ? producer->second.type != as::CatalogAssetType::GltfImport ||
                     as::deriveAssetGuid(producer->second.root, record->source.subKey) != asset
               : producer->second.type != record->type || producer->second.root != asset))
    return R::Err(ca::RebuildError::UnsupportedProducer);
  std::optional<std::string> replacement;
  if (const auto name = textureName(producer->first)) {
    const auto registered = state_->activeTextures.find(*name);
    if (state_->project.empty() || !state_->config.generation || derived || record->type != as::CatalogAssetType::Texture ||
        textureKind(producer->first) != TextureKind::Color || registered == state_->activeTextures.end() ||
        registered->second.guid != asset || registered->second.kind != TextureKind::Color)
      return R::Err(ca::RebuildError::UnsupportedProducer);
    replacement.emplace(*name);
  }
  const auto guid = newGuid();
  if (!guid) return R::Err(ca::RebuildError::Unavailable);
  ca::RebuildJob job{{*guid}, asset, ca::RebuildState::Running, {}, {}};
  state_->running.emplace(State::Work{job, &producer->second, std::move(catalog),
                                      Clock::now() + state_->config.timeout, std::move(replacement)});
  state_->changed.notify_one();
  return R::Ok(job);
}
ca::RebuildStatusResult AssetRebuildHost::status(std::optional<ca::RebuildId> id) const {
  std::lock_guard lock(state_->mutex);
  using R = ca::RebuildStatusResult;
  ca::RebuildStatus status{state_->config.generation, {}};
  if (state_->running && (!id || state_->running->job.id == *id)) {
    status.job = state_->running->job;
  } else {
    for (const auto& job : state_->completed) {
      if (!id || job.id == *id) { status.job = job; break; }
    }
  }
  if (id && !status.job) return R::Err(ca::RebuildError::UnknownJob);
  return R::Ok(std::move(status));
}
std::optional<std::string> AssetRebuildHost::displayName(as::AssetGuid asset) const {
  const auto found = state_->activeNames.find(asset);
  return found == state_->activeNames.end() ? std::nullopt : std::optional{found->second};
}
ca::RenameTextureResult AssetRebuildHost::renameTexture(
    const ca::RenameTextureRequest& request, std::shared_ptr<const as::AssetCatalog> catalog) {
  using E = ca::RenameTextureError;
  using R = ca::RenameTextureResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty() || !state_->config.generation) return R::Err(E::Unavailable);
  if (auto error = state_->slotError()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::Unavailable);
  }
  if (state_->config.generation != request.expectedGeneration || state_->metadataChanged() ||
      state_->registered != state_->activeRegistered || state_->committedMaterials != state_->activeMaterials ||
      state_->textureRegistryChanged) return R::Err(E::StaleSnapshot);
  if (!ca::validImportName(request.displayName)) return R::Err(E::InvalidName);
  if (!catalog) return R::Err(E::CatalogUnavailable);
  const auto* record = catalog->find(request.asset);
  if (!record) return R::Err(E::UnknownAsset);
  const auto key = as::toString(record->source);
  const auto name = textureName(key);
  const auto producer = state_->producers.find(key);
  if (!name || !record->source.subKey.empty() || record->type != as::CatalogAssetType::Texture ||
      textureKind(key) != TextureKind::Color || producer == state_->producers.end() ||
      producer->second.root != request.asset || producer->second.type != record->type)
    return R::Err(E::UnsupportedProducer);
  const auto unit = state_->activeTextures.find(*name);
  if (unit == state_->activeTextures.end() || unit->second.guid != request.asset || unit->second.kind != TextureKind::Color)
    return R::Err(E::UnsupportedProducer);
  if (labelClaimed(state_->activeNames, state_->activeTextures, request.asset, request.displayName))
    return R::Err(E::NameConflict);
  const auto override = state_->activeNames.find(request.asset);
  if (request.displayName == (override == state_->activeNames.end() ? *name : std::string_view(override->second)))
    return R::Err(E::Unchanged);
  if (state_->activeNames.size() >= kNameLimit && override == state_->activeNames.end())
    return R::Err(E::Unavailable);
  const auto id = newGuid();
  if (!id) return R::Err(E::Unavailable);
  ca::RebuildJob job{{*id}, request.asset, ca::RebuildState::Running, {}, {}};
  state_->running.emplace(State::Work{job, &producer->second, std::move(catalog), Clock::now() + state_->config.timeout,
                                      std::nullopt, request});
  state_->changed.notify_one();
  return R::Ok(job);
}
std::optional<std::string> AssetRebuildHost::logicalPath(as::AssetGuid asset) const {
  const auto found = state_->activeLocations.find(asset);
  return found == state_->activeLocations.end() ? std::nullopt : std::optional{found->second};
}
ca::MoveTextureResult AssetRebuildHost::moveTexture(
    const ca::MoveTextureRequest& request, std::shared_ptr<const as::AssetCatalog> catalog) {
  using E = ca::MoveTextureError;
  using R = ca::MoveTextureResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty() || !state_->config.generation) return R::Err(E::Unavailable);
  if (auto error = state_->slotError()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::Unavailable);
  }
  if (state_->config.generation != request.expectedGeneration || state_->metadataChanged() ||
      state_->registered != state_->activeRegistered || state_->committedMaterials != state_->activeMaterials ||
      state_->textureRegistryChanged) return R::Err(E::StaleSnapshot);
  if (!ca::validTextureLogicalPath(request.expectedLogicalPath) ||
      !ca::validTextureLogicalPath(request.destinationLogicalPath)) return R::Err(E::InvalidPath);
  if (!catalog) return R::Err(E::CatalogUnavailable);
  const auto* record = catalog->find(request.asset);
  if (!record) return R::Err(E::UnknownAsset);
  const auto key = as::toString(record->source);
  const auto name = textureName(key);
  const auto producer = state_->producers.find(key);
  if (!name || !record->source.subKey.empty() || record->type != as::CatalogAssetType::Texture ||
      textureKind(key) != TextureKind::Color || producer == state_->producers.end() ||
      producer->second.root != request.asset || producer->second.type != record->type)
    return R::Err(E::UnsupportedProducer);
  const auto unit = state_->activeTextures.find(*name);
  if (unit == state_->activeTextures.end() || unit->second.guid != request.asset || unit->second.kind != TextureKind::Color)
    return R::Err(E::UnsupportedProducer);
  const auto current = effectiveLogicalPath(state_->activeLocations, request.asset, *name);
  if (current != request.expectedLogicalPath) return R::Err(E::StaleSnapshot);
  if (current == request.destinationLogicalPath) return R::Err(E::Unchanged);
  const auto folder = [](std::string_view path) { return path.substr(0, path.rfind('/')); };
  if (folder(current) == folder(request.destinationLogicalPath)) return R::Err(E::InvalidPath);
  if (pathClaimed(state_->activeLocations, state_->activeTextures, request.asset, request.destinationLogicalPath))
    return R::Err(E::PathConflict);
  if (state_->activeLocations.size() >= kNameLimit && !state_->activeLocations.contains(request.asset))
    return R::Err(E::Unavailable);
  const auto id = newGuid();
  if (!id) return R::Err(E::Unavailable);
  ca::RebuildJob job{{*id}, request.asset, ca::RebuildState::Running, {}, {}};
  state_->running.emplace(State::Work{job, &producer->second, std::move(catalog), Clock::now() + state_->config.timeout,
                                      std::nullopt, std::nullopt, std::nullopt, request});
  state_->changed.notify_one();
  return R::Ok(job);
}
ca::TextureRemovalCheckResult AssetRebuildHost::checkTextureRemoval(
    const ca::TextureRemovalTarget& target, std::shared_ptr<const as::AssetCatalog> catalog) {
  using E = ca::TextureRemovalError;
  using R = ca::TextureRemovalCheckResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty() || !state_->config.generation) return R::Err(E::Unavailable);
  if (const auto error = state_->slotError()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::Unavailable);
  }
  if (state_->config.generation != target.expectedGeneration) return R::Err(E::StaleCheck);
  if (!catalog) return R::Err(E::CatalogUnavailable);
  const auto* record = catalog->find(target.asset);
  if (!record) return R::Err(E::UnknownAsset);
  const auto key = as::toString(record->source);
  const auto name = textureName(key);
  const auto producer = state_->producers.find(key);
  if (!name || !record->source.subKey.empty() || record->type != as::CatalogAssetType::Texture ||
      textureKind(key) != TextureKind::Color || producer == state_->producers.end() ||
      producer->second.root != target.asset || producer->second.type != record->type)
    return R::Err(E::UnsupportedProducer);
  const auto unit = state_->activeTextures.find(*name);
  if (unit == state_->activeTextures.end() || unit->second.guid != target.asset ||
      unit->second.kind != TextureKind::Color) return R::Err(E::UnsupportedProducer);
  const auto id = newGuid();
  if (!id) return R::Err(E::Unavailable);
  ca::TextureRemovalCheck check{{*id}, target};
  state_->removalProof.reset();
  state_->removalCheck.emplace(State::RemovalRead{check, std::string(*name), std::move(catalog),
                                                Clock::now() + state_->config.timeout});
  state_->changed.notify_one();
  return R::Ok(std::move(check));
}
ca::TextureRemovalCheckResult AssetRebuildHost::textureRemovalCheckStatus(ca::TextureRemovalCheckId id) const {
  using R = ca::TextureRemovalCheckResult;
  std::lock_guard lock(state_->mutex);
  if (!state_->removalCheck || state_->removalCheck->check.id != id)
    return R::Err(ca::TextureRemovalError::UnknownCheck);
  auto check = state_->removalCheck->check;
  if (check.state == ca::TextureRemovalState::Ready &&
      (!state_->removalProof || Clock::now() >= state_->removalProof->expires)) {
    check.state = ca::TextureRemovalState::StaleCheck;
    check.diagnostic = "The check has expired or was consumed. Check again.";
  }
  return R::Ok(std::move(check));
}
ca::RemoveTextureResult AssetRebuildHost::removeTexture(
    const ca::TextureRemovalTarget& target, ca::TextureRemovalCheckId id) {
  using E = ca::TextureRemovalError;
  using R = ca::RemoveTextureResult;
  std::lock_guard lock(state_->mutex);
  if (!state_->removalCheck || state_->removalCheck->check.id != id) return R::Err(E::UnknownCheck);
  const auto& checked = *state_->removalCheck;
  if (checked.check.target != target || state_->config.generation != target.expectedGeneration ||
      checked.check.state != ca::TextureRemovalState::Ready || !state_->removalProof ||
      Clock::now() >= state_->removalProof->expires) return R::Err(E::StaleCheck);
  if (const auto error = state_->admissionLocked()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::StaleCheck);
  }
  const auto idValue = newGuid();
  if (!idValue) return R::Err(E::Unavailable);
  ca::RebuildJob job{{*idValue}, target.asset, ca::RebuildState::Running, {}, {}};
  const auto producer = state_->producers.find(textureKey(checked.token, TextureKind::Color));
  if (producer == state_->producers.end()) return R::Err(E::StaleCheck);
  state_->running.emplace(State::Work{job, &producer->second, checked.catalog, Clock::now() + state_->config.timeout,
                                      checked.token, {}, std::move(state_->removalProof)});
  state_->removalProof.reset();
  state_->changed.notify_one();
  return R::Ok(job);
}
fs::path AssetRebuildHost::generationCatalogSource() const {
  if (state_->project.empty()) return {};
  return state_->active.parent_path() / kManagedSource;
}
ca::ImportStartResult AssetRebuildHost::startImport(std::string_view name, ca::ImportMode mode,
                                                   std::shared_ptr<const as::AssetCatalog> catalog) {
  using R = ca::ImportStartResult;
  using E = ca::ImportError;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty() || !ca::validImportName(name)) return R::Err(E::Unavailable);
  if (auto error = state_->slotError()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::Unavailable);
  }
  if (!catalog) return R::Err(E::CatalogUnavailable);
  if (state_->metadataChanged())
    return R::Err(E::ActivationRequired);
  const auto committed = state_->registered.find(name);
  std::optional<as::AssetGuid> root;
  if (mode == ca::ImportMode::Register) {
    if (committed != state_->registered.end() || state_->producers.contains(managedSourceKey(name)))
      return R::Err(E::NameConflict);
    if (state_->registered != state_->activeRegistered || state_->committedMaterials != state_->activeMaterials ||
        state_->textureRegistryChanged)
      return R::Err(E::ActivationRequired);
  } else {
    if (committed == state_->registered.end() || state_->activeRegistered.contains(name))
      return R::Err(E::NothingToResume);
    auto expected = state_->activeRegistered;
    expected.emplace(committed->first, committed->second);
    if (expected != state_->registered || state_->committedMaterials != state_->activeMaterials ||
        state_->textureRegistryChanged)
      return R::Err(E::ActivationRequired);
    root = committed->second;
  }
  const auto guid = newGuid();
  if (!guid) return R::Err(E::Unavailable);
  ca::ImportJob job{{*guid}, std::string(name), mode, ca::RebuildState::Running, {}, {}, {}};
  state_->importing.emplace(State::ImportWork{job, root, std::move(catalog), Clock::now() + state_->config.timeout});
  state_->changed.notify_one();
  return R::Ok(job);
}
ca::ImportStatusResult AssetRebuildHost::importStatus(std::optional<ca::ImportId> id) const {
  using R = ca::ImportStatusResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty()) return R::Err(ca::ImportError::Unavailable);
  ca::ImportStatus status{state_->config.generation, {}};
  if (state_->importing && (!id || state_->importing->job.id == *id)) status.job = state_->importing->job;
  else for (const auto& job : state_->imported) {
    if (!id || job.id == *id) { status.job = job; break; }
  }
  if (id && !status.job) return R::Err(ca::ImportError::UnknownJob);
  return R::Ok(std::move(status));
}
bool AssetRebuildHost::ownsMaterial(const as::AssetGuid& guid) const {
  return !state_->project.empty() && std::ranges::any_of(state_->activeMaterials, [&](const auto& row) {
    return row.second.guid == guid;
  });
}
std::optional<ca::GenerationId> AssetRebuildHost::activeGeneration() const {
  return state_->config.generation;
}
ca::MaterialSaveResult AssetRebuildHost::saveMaterial(const ca::MaterialSave& request, std::shared_ptr<const as::AssetCatalog> catalog,
    ca::MaterialRecipeVersion version) {
  using R = ca::MaterialSaveResult;
  using E = ca::MaterialError;
  const auto target = ca::materialTarget(request);
  const auto validValues = [version](const auto& values) {
    return ca::validMaterialValues(values) &&
        ca::representsMaterialValues(values, version);
  };
  const bool valid = std::visit([&](const auto& input) {
    using T = std::decay_t<decltype(input)>;
    if constexpr (std::is_same_v<T, ca::CreateMaterial>)
      return ca::validImportName(input.name) && validValues(input.values);
    else if constexpr (std::is_same_v<T, ca::CopyMaterial>)
      return ca::validImportName(input.name) && input.material != as::AssetGuid{} && validValues(input.values);
    else if constexpr (std::is_same_v<T, ca::EditMaterial>)
      return input.material != as::AssetGuid{} && (!input.activeGeneration || input.activeGeneration->value != as::AssetGuid{}) &&
          validValues(input.expected) && validValues(input.values);
    else
      return input.material != as::AssetGuid{};
  }, request);
  if (!valid) return R::Err(E::InvalidRecipe);
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty()) return R::Err(E::Unavailable);
  if (auto error = state_->slotError()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::Unavailable);
  }
  if (!catalog) return R::Err(E::CatalogUnavailable);
  if (state_->metadataChanged()) return R::Err(E::ActivationRequired);
  const auto id = newGuid();
  if (!id) return R::Err(E::Unavailable);
  ca::MaterialJob job{{*id}, target, ca::RebuildState::Running, {}, {}, {}};
  state_->materialWork.emplace(State::MaterialWork{job, request, version, std::move(catalog),
                                                  Clock::now() + state_->config.timeout});
  state_->changed.notify_one();
  return R::Ok(job);
}
ca::MaterialStatusResult AssetRebuildHost::materialStatus(std::optional<ca::MaterialJobId> id) const {
  using R = ca::MaterialStatusResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty()) return R::Err(ca::MaterialError::Unavailable);
  ca::MaterialStatus status{state_->config.generation, {}, state_->dirty};
  if (state_->materialWork && (!id || state_->materialWork->job.id == *id))
    status.job = state_->materialWork->job;
  else for (const auto& job : state_->materialJobs) {
    if (!id || job.id == *id) { status.job = job; break; }
  }
  if (id && !status.job) return R::Err(ca::MaterialError::UnknownJob);
  return R::Ok(std::move(status));
}
ca::TextureImportStartResult AssetRebuildHost::startTextureImport(
    std::string_view name, ca::ImportMode mode, std::shared_ptr<const as::AssetCatalog> catalog, TextureKind kind) {
  using E = ca::ImportError;
  using R = ca::TextureImportStartResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty() || !ca::validImportName(name)) return R::Err(E::Unavailable);
  if (auto error = state_->slotError()) {
    if (*error == ca::RebuildError::Busy) return R::Err(E::Busy);
    if (*error == ca::RebuildError::WorkspaceNeedsRepair) return R::Err(E::WorkspaceNeedsRepair);
    return R::Err(E::Unavailable);
  }
  if (!catalog) return R::Err(E::CatalogUnavailable);
  if (state_->metadataChanged()) return R::Err(E::ActivationRequired);
  const auto found = state_->committedTextures.find(name);
  if (kind == TextureKind::Color &&
      labelClaimed(state_->committedNames, state_->committedTextures,
                   mode == ca::ImportMode::Resume && found != state_->committedTextures.end()
                       ? found->second.guid : as::AssetGuid{}, name))
    return R::Err(E::NameConflict);
  if (kind == TextureKind::Color &&
      pathClaimed(state_->committedLocations, state_->committedTextures,
                  mode == ca::ImportMode::Resume && found != state_->committedTextures.end()
                      ? found->second.guid : as::AssetGuid{}, fallbackPath(name)))
    return R::Err(E::NameConflict);
  if (mode == ca::ImportMode::Register) {
    if (found != state_->committedTextures.end()) return R::Err(E::NameConflict);
    if (state_->textureRegistryChanged) return R::Err(E::ActivationRequired);
  } else {
    if (found == state_->committedTextures.end() || found->second.kind != kind || state_->activeTextures.contains(name))
      return R::Err(E::NothingToResume);
    if (state_->committedTextures.size() != state_->activeTextures.size() + 1 ||
        !std::ranges::all_of(state_->activeTextures, [&](const auto& entry) {
          auto current = state_->committedTextures.find(entry.first);
          return current != state_->committedTextures.end() && current->second == entry.second;
        })) return R::Err(E::ActivationRequired);
  }
  if (state_->activeRegistered != state_->registered || state_->activeMaterials != state_->committedMaterials)
    return R::Err(E::ActivationRequired);
  const auto id = newGuid();
  if (!id) return R::Err(E::Unavailable);
  ca::TextureImportJob job{{*id}, std::string(name), mode, ca::RebuildState::Running, {}, {}, {}};
  const auto resumeInput = mode == ca::ImportMode::Resume ? std::optional{found->second} : std::nullopt;
  state_->textureWork.emplace(State::TextureWork{job, kind, resumeInput, std::move(catalog), Clock::now() + state_->config.timeout});
  state_->changed.notify_one();
  return R::Ok(job);
}
ca::TextureImportStatusResult AssetRebuildHost::textureImportStatus(std::optional<ca::TextureImportId> id, TextureKind kind) const {
  using R = ca::TextureImportStatusResult;
  std::lock_guard lock(state_->mutex);
  if (state_->project.empty()) return R::Err(ca::ImportError::Unavailable);
  ca::TextureImportStatus status{state_->config.generation, {}, state_->dirty};
  if (state_->textureWork && state_->textureWork->kind == kind && (!id || state_->textureWork->job.id == *id))
    status.job = state_->textureWork->job;
  else for (const auto& [jobKind, job] : state_->textureJobs) {
    if (jobKind == kind && (!id || job.id == *id)) { status.job = job; break; }
  }
  if (id && !status.job) return R::Err(ca::ImportError::UnknownJob);
  return R::Ok(std::move(status));
}
}  // namespace atlantis::runtime
