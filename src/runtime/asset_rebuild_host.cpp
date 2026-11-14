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
constexpr std::string_view kManagedSource = "assets/managed-catalog-source.txt";
constexpr std::string_view kProjectDirty = ".atlantis-import-dirty";
constexpr std::string_view kMaterialSources = "assets/managed-material-sources";
constexpr std::string_view kMaterialLeaf = "source.material.txt";
constexpr std::string_view kTextureSources = "assets/managed-texture-sources";
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
constexpr std::array kTextureLeaves{"source.png", "normal.png", "metallic-roughness.png"};
constexpr std::array kTextureRoles{"BaseColor", "Normal", "MetallicRoughness"};
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

std::optional<std::string> fileDigest(const fs::path& root, const fs::path& relative, DWORD limit) {
  if (!exactTexturePath(root, relative)) return {};
  const auto path = root / relative;
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!file || !GetFileInformationByHandle(file.get(), &info) || info.nFileSizeHigh || !info.nFileSizeLow ||
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

std::optional<Producers> readManagedView(const fs::path& root, const fs::path& inventory) {
  if (!safeRelative(root, inventory) || !safeRelative(root, fs::path(kManagedSource))) return std::nullopt;
  const auto producerText = readText(root / inventory);
  const auto catalogText = readText(root / kManagedSource);
  if (!producerText || !catalogText) return std::nullopt;
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
                     const TextureInputs& textures = {}) {
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
  if (!managedInventory.empty()) {
    const std::uint64_t version = hasPackedMaps ? 6u : hasNormals ? 5u : !textures.empty() ? 4u : materials.empty() ? 2u : 3u;
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
      if (hasNormals || hasPackedMaps)
        entry.set("colorSpace", json::Value::string(input.kind == TextureKind::Color ? "Srgb" : "Unorm"));
      if (hasPackedMaps)
        entry.set("role", json::Value::string(kTextureRoles[static_cast<std::size_t>(input.kind)]));
      sources.set(key, std::move(entry));
    }
    value.set("textureSources", std::move(sources));
  }
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
  struct Work {
    ca::RebuildJob job;
    const Producer* producer;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
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
    if (!writer || (!project.empty() && !projectWriter) || running || importing || materialWork || textureWork)
      return ca::RebuildError::Busy;
    if (dirty) return ca::RebuildError::WorkspaceNeedsRepair;
    return std::nullopt;
  }

  std::optional<ca::RebuildError> admissionLocked() const {
    if (auto error = slotError()) return error;
    if (!project.empty() && (registered != activeRegistered || committedMaterials != activeMaterials || textureRegistryChanged))
      return ca::RebuildError::Unavailable;
    return std::nullopt;
  }

  struct Prepared { ca::GenerationId id; fs::path directory; };
  template<class Interrupted>
  std::variant<Prepared, End> prepare(const as::AssetCatalog& catalog, const Producers& view, Interrupted interrupted,
                                      std::optional<Prepared>* ownedStaging = nullptr) {
    auto files = outputFiles(staging, catalog, view);
    if (!files) return fail(Stage::Validation, Code::InvalidOutput, "Staging output is missing or unsafe.", true);
    Materials materials;
    TextureInputs textures;
    if (!project.empty()) {
      files->push_back(inventoryRelative);
      files->push_back(fs::path(kManagedSource));
      auto inputs = materialInputs(staging, view);
      if (!inputs) return fail(Stage::Validation, Code::InvalidOutput, "Invalid staged material inputs.", true);
      materials = std::move(*inputs);
      for (const auto& [name, unit] : materials)
        files->push_back(fs::path(kMaterialSources) / materialDirectory(name) / kMaterialLeaf);
      auto input = textureInputs(staging, view, catalog);
      if (!input) return fail(Stage::Validation, Code::InvalidOutput, "Invalid staged texture evidence.", true);
      textures = std::move(*input);
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
      if (!copyOutput(staging / file, temp / file, interrupted))
        return interrupted() ? timedOut(Stage::Publication)
                             : fail(Stage::Publication, Code::PublishFailed, "Cannot copy a complete generation.");
    }
    const auto data = manifest(owner, id, view, project.empty() ? fs::path{} : inventoryRelative, materials, textures);
    if (!writeText(temp / kManifest, json::write(data)))
      return fail(Stage::Publication, Code::PublishFailed, "Cannot write generation manifest.");
    if (!textures.empty()) {
      const auto copiedCatalog = readCatalog(temp);
      const auto copiedInput = copiedCatalog ? textureInputs(temp, view, *copiedCatalog) : std::nullopt;
      if (!copiedInput || manifest(owner, id, view, inventoryRelative, materials, *copiedInput) != data)
        return fail(Stage::Validation, Code::InvalidOutput, "Prepared texture generation differs from staging.", true);
    }
    return Prepared{id, temp};
  }

  bool publish(const Prepared& generation) const {
    return MoveFileExW(generation.directory.c_str(),
                      (publication / as::toString(generation.id.value)).c_str(), 0);
  }

  std::variant<ca::GenerationId, End> build(const Work& work) {
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    if (interrupted()) return timedOut(Stage::Launch);
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
    if (!initialTextures || *initialTextures != activeTextures)
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
    if (registeredTextures(project) != activeTextures)
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
      if (!registryTextures || *registryTextures != activeTextures) {
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
                   input.metallicRoughnessMapAsset},
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
      const auto expectedManifest = manifest(owner, prepared->id, *view, inventoryRelative, expected, *textureView);
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
            manifest(owner, prepared->id, *frozen, inventoryRelative, expected, *copied) == expectedManifest;
      };

      std::lock_guard commit(publicationCommit);
      if (!matchesPrepared(prepared->directory))
        return failJob(MS::Validation, MC::InvalidOutput, "The prepared generation differs from the intended material source, catalog or texture evidence.");
      if (interrupted()) return failJob(MS::SourceCommit, MC::TimedOut, "Material job stopped before commit.");
      if (registeredMaterials(project) != before || readRegistrations(project) != imports ||
          registeredTextures(project) != activeTextures) {
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
        if (!realDirectory(prepared->directory) && matchesPrepared(destination)) {
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
      if (!before || !imports || !materials) {
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
      const auto expectedManifest = manifest(owner, prepared->id, *view, inventoryRelative, *materials, *inputs);
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
            manifest(owner, prepared->id, *frozen, inventoryRelative, *materials, *copied) == expectedManifest;
      };
      std::lock_guard commit(publicationCommit);
      const bool preparedMatches = matchesPrepared(prepared->directory);
      const auto current = registeredTextures(project);
      if (resume && (!current || !current->contains(job.name) || current->at(job.name) != expected.at(job.name)))
        registration = RS::Unknown;
      if (current != before || readRegistrations(project) != imports ||
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
        if (realDirectory(prepared->directory) || !matchesPrepared(destination)) {
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
      {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return stopping || running || importing || materialWork || textureWork; });
        if (stopping && !running && !importing && !materialWork && !textureWork) return;
        work = running;
        import = importing;
        material = materialWork;
        texture = textureWork;
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
      auto result = build(*work);
      auto& job = work->job;
      const auto* failure = std::get_if<End>(&result);
      bool repair = failure && failure->repair;
      if (!repair) {
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
      auto frozen = readManagedView(selected, inventory);
      const auto frozenInventory = readText(selected / inventory);
      if (!frozen || !frozenInventory || inventoryHeader(*frozenInventory) != inventoryHeader(state->inventory) ||
          baseProducers(*frozen) != baseProducers(state->producers) || !matchesManagedCatalog(*catalog, *frozen))
        return R::Err("Invalid managed generation metadata.");
      auto materials = materialInputs(selected, *frozen);
      if (!materials || !matchesFrozenMaterials(*catalog, *materials))
        return R::Err("Managed generation material sources are missing or invalid.");
      auto textures = textureInputs(selected, *frozen, *catalog);
      if (!textures) return R::Err("Managed generation texture evidence is missing or invalid.");
      state->activeTextures = textureRegistrations(*frozen, *textures);
      selectedTextures = std::move(*textures);
      state->activeMaterials = std::move(*materials);
      state->producers = std::move(*frozen);
    }
    const auto managedInventory = state->project.empty() ? fs::path{} : inventory;
    if (*selectedManifest != manifest(state->owner, *generation, state->producers, managedInventory,
                                     state->activeMaterials, selectedTextures))
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
      state->dirty = state->dirty || !registered || !materials || !textures ||
          !safeRelative(state->project, fs::path(kProjectDirty), true) ||
          fs::exists(state->project / kProjectDirty, ec) || ec || !pending || !pending->empty();
      if (registered) state->registered = std::move(*registered);
      if (materials) state->committedMaterials = std::move(*materials);
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
  if (producer == state_->producers.end() || textureName(producer->first) ||
      (derived ? producer->second.type != as::CatalogAssetType::GltfImport ||
                     as::deriveAssetGuid(producer->second.root, record->source.subKey) != asset
               : producer->second.type != record->type || producer->second.root != asset))
    return R::Err(ca::RebuildError::UnsupportedProducer);
  const auto guid = newGuid();
  if (!guid) return R::Err(ca::RebuildError::Unavailable);
  ca::RebuildJob job{{*guid}, asset, ca::RebuildState::Running, {}, {}};
  state_->running.emplace(State::Work{job, &producer->second, std::move(catalog), Clock::now() + state_->config.timeout});
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
  const auto found = state_->committedTextures.find(name);
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
