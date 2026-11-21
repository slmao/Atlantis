#pragma once

#include "prefab_projection.h"
#include "scene_recovery_store.h"

#include <atlantis/asset_system/asset_catalog_source.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace atlantis::runtime::project_assets {
namespace fs = std::filesystem;
namespace as = asset_system;
namespace ca = connection::assets;

struct Deleter { void operator()(void* value) const { CloseHandle(value); } };
using Handle = std::unique_ptr<void, Deleter>;

inline std::string text(const fs::path& path) {
  const auto bytes = path.generic_u8string();
  return {bytes.begin(), bytes.end()};
}
struct FileProof {
  Handle file;
  FILE_ID_INFO id{};
  BY_HANDLE_FILE_INFORMATION info{};
  std::string key() const {
    std::string result(reinterpret_cast<const char*>(&id.VolumeSerialNumber), sizeof(id.VolumeSerialNumber));
    result.append(reinterpret_cast<const char*>(id.FileId.Identifier), sizeof(id.FileId.Identifier));
    result += "/" + std::to_string(info.ftLastWriteTime.dwHighDateTime) + "/" +
        std::to_string(info.ftLastWriteTime.dwLowDateTime) + "/" +
        std::to_string(info.nFileSizeHigh) + "/" + std::to_string(info.nFileSizeLow);
    return result;
  }
};

inline std::optional<FileProof> checked(const fs::path& path, bool directory, bool reading = false) {
  WIN32_FIND_DATAW entry{};
  const auto found = FindFirstFileW(path.c_str(), &entry);
  if (found == INVALID_HANDLE_VALUE) return {};
  FindClose(found);
  if (path.filename().native() != entry.cFileName) return {};
  const HANDLE native = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | (reading ? GENERIC_READ : 0),
      FILE_SHARE_READ | (directory ? FILE_SHARE_WRITE : 0), nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
  if (native == INVALID_HANDLE_VALUE) return {};
  FileProof out{Handle(native)};
  if (!GetFileInformationByHandle(native, &out.info) ||
      !GetFileInformationByHandleEx(native, FileIdInfo, &out.id, sizeof(out.id)) ||
      (out.info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      ((out.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      (!directory && out.info.nNumberOfLinks != 1)) return {};
  if (directory) {
    FILE_CASE_SENSITIVE_INFO bits{};
    if (!GetFileInformationByHandleEx(native, FileCaseSensitiveInfo, &bits, sizeof(bits)) ||
        (bits.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR)) return {};
  }
  return out;
}

inline std::vector<FileProof> pin(const fs::path& root) {
  if (!root.is_absolute() || GetDriveTypeW(root.root_path().c_str()) != DRIVE_FIXED) return {};
  std::vector<FileProof> pins;
  auto path = root.root_path();
  for (const auto& part : root.relative_path()) {
    if (part == L"." || part == L"..") return {};
    path /= part;
    auto proof = checked(path, true);
    if (!proof) return {};
    pins.push_back(std::move(*proof));
  }
  wchar_t type[16]{};
  if (pins.empty() || !GetVolumeInformationByHandleW(pins.back().file.get(), nullptr, 0, nullptr, nullptr,
      nullptr, type, 16) || std::wstring_view(type) != L"NTFS") return {};
  return pins;
}

inline std::optional<std::string> read(const FileProof& file, DWORD limit) {
  if (file.info.nFileSizeHigh || file.info.nFileSizeLow > limit || file.info.nFileSizeLow == 0) return {};
  DWORD count = 0;
  std::string content(file.info.nFileSizeLow, '\0');
  if (!ReadFile(file.file.get(), content.data(), file.info.nFileSizeLow, &count, nullptr) ||
      count != file.info.nFileSizeLow) return {};
  return content;
}

inline void incomplete(ca::ProjectRoot& root, std::string reason) {
  root.coverage = ca::ProjectCoverage::Incomplete;
  root.diagnostic = std::move(reason);
  for (auto& row : root.sources) {
    row.registration = ca::ProjectRegistration::Unknown;
    row.producer.reset();
    row.outputs.clear();
    row.newerPrefabSource = false;
  }
}

using Observe = std::function<bool(ca::ProjectSource&, const fs::path&, const FileProof&)>;
struct TreeRead {
  std::size_t remaining;
  const std::atomic<bool>& stop;
  const Observe& observe;
  ca::ProjectRoot& result;
  bool complete = true;

  void descend(const fs::path& location, const fs::path& relative, unsigned depth) {
    std::error_code err;
    auto at = fs::directory_iterator(location, err);
    while (!err && at != fs::directory_iterator{}) {
      if (stop || remaining == 0) { complete = false; return; }
      --remaining;
      const auto file = at->path();
      const auto path = relative / file.filename();
      const auto posix = text(path);
      if (!ca::validProjectPath(posix)) { complete = false; at.increment(err); continue; }
      const auto existing = at->symlink_status(err);
      if (err) break;
      const bool directory = fs::is_directory(existing);
      auto proof = checked(file, directory, !directory && result.kind == ca::ProjectRootKind::Prefabs);
      if (!proof) { complete = false; at.increment(err); continue; }
      if (directory && depth >= 4) { complete = false; at.increment(err); continue; }
      const auto stamp = digestSceneBytes(proof->key());
      if (!stamp) { complete = false; at.increment(err); continue; }
      ca::ProjectSource row;
      row.path = posix;
      row.kind = directory ? ca::ProjectSourceKind::Directory : ca::ProjectSourceKind::Other;
      row.evidence = *stamp;
      if (!directory && !observe(row, file, *proof)) complete = false;
      result.sources.push_back(std::move(row));
      if (directory) descend(file, path, depth + 1);
      const auto final = checked(file, directory);
      if (!final || final->key() != proof->key()) complete = false;
      at.increment(err);
    }
    if (err) complete = false;
  }
};

inline ca::ProjectRoot readOnce(const fs::path& root, ca::ProjectRootKind kind, std::size_t limit,
                               const std::atomic<bool>& stop, const Observe& observe) {
  ca::ProjectRoot result{kind, ca::ProjectCoverage::Unavailable, "Source root is unavailable.", {}};
  auto pins = pin(root);
  if (pins.empty()) return result;
  result.coverage = ca::ProjectCoverage::Complete;
  result.diagnostic.clear();
  TreeRead work{limit, stop, observe, result};
  work.descend(root, {}, 0);
  auto final = checked(root, true);
  if (!work.complete || !final || final->key() != pins.back().key())
    incomplete(result, "The source inventory is incomplete or changed during the scan.");
  std::ranges::sort(result.sources, {}, &ca::ProjectSource::path);
  return result;
}

inline ca::ProjectRoot treeRead(const fs::path& root, ca::ProjectRootKind kind, std::size_t limit,
                               const std::atomic<bool>& stop, const Observe& observe) {
  auto result = readOnce(root, kind, limit, stop, observe);
  if (result.coverage == ca::ProjectCoverage::Complete &&
      result != readOnce(root, kind, limit, stop, observe))
    incomplete(result, "The source inventory changed during the scan.");
  return result;
}

struct PrefabEvidence { as::AssetGuid guid; std::string revision; };
using FrozenPrefabs = std::map<std::string, PrefabEvidence>;
inline ca::ProjectRoot checkout(const fs::path& assetRoot, const FrozenPrefabs& frozen, std::size_t limit,
                                const std::atomic<bool>& stop) {
  if (assetRoot.empty()) return {ca::ProjectRootKind::Prefabs, ca::ProjectCoverage::Unavailable, "Checkout root is unavailable.", {}};
  const auto folder = assetRoot / L"prefabs";
  const auto snapshot = [&](ca::ProjectSource& row, const fs::path& local, const FileProof& file) {
    const auto slash = row.path.find('/');
    if (slash == std::string::npos || !row.path.starts_with("prefab_") ||
        row.path.substr(slash + 1) != "source.scene.txt") return true;
    row.kind = ca::ProjectSourceKind::Prefab;
    const auto name = row.path.substr(7, slash - 7);
    if (!connection::authoring::validSaveAsName(name)) return false;
    const auto manifestPath = local.parent_path() / L"catalog-source.txt";
    const auto manifest = checked(manifestPath, false, true);
    const auto bytes = manifest ? read(*manifest, 512) : std::nullopt;
    const auto source = read(file, 64 * 1024);
    if (!bytes || !source) return false;
    const auto catalog = as::parseAssetCatalogSource(*bytes);
    const auto parsed = as::parseSceneSource(*source);
    if (catalog.isErr() || catalog.value().entries().size() != 1 || parsed.isErr() ||
        !source->starts_with("atlantis_scene_source_version: 8\n") ||
        as::serializeSceneSource(parsed.value()) != *source) return false;
    const auto& entry = catalog.value().entries().front();
    const auto key = "prefabs/" + row.path;
    if (entry.path != key || entry.type != as::CatalogAssetType::Scene || entry.root != as::CatalogRoot::Assets ||
        as::serializeAssetCatalogSource({entry}) != *bytes) return false;
    const auto current = digestSceneBytes(*source);
    const auto evidence = digestSceneBytes(file.key() + manifest->key() + *bytes + *source);
    const auto final = checked(manifestPath, false);
    if (!current || !evidence || !final || final->key() != manifest->key()) return false;
    row.registration = ca::ProjectRegistration::Registered;
    row.producer = entry.guid;
    row.evidence = *evidence;
    const auto active = frozen.find(key);
    if (active != frozen.end() && active->second.guid == entry.guid) {
      row.outputs.push_back(entry.guid);
      row.newerPrefabSource = *current != active->second.revision;
    }
    return true;
  };
  auto result = treeRead(folder, ca::ProjectRootKind::Prefabs, limit, stop, snapshot);
  std::erase_if(result.sources, [](const auto& row) {
    return row.kind != ca::ProjectSourceKind::Directory && row.kind != ca::ProjectSourceKind::Prefab;
  });
  std::set<as::AssetGuid> unique;
  for (const auto& row : result.sources) {
    if (row.producer && !unique.insert(*row.producer).second) {
      incomplete(result, "Prefab registrations have ambiguous identities.");
      break;
    }
  }
  return result;
}

inline void verifyLimits(ca::ProjectScan& scan) {
  std::map<as::AssetGuid, unsigned> links;
  std::size_t count = 0;
  for (auto& root : scan.roots)
    for (auto& row : root.sources) {
      count += row.outputs.size();
      for (const auto& guid : row.outputs) ++links[guid];
    }
  for (auto& root : scan.roots) {
    if (count > ca::kProjectLinks) {
      incomplete(root, "The scan exceeded its asset-link limit.");
      continue;
    }
    for (auto& row : root.sources) {
      std::erase_if(row.outputs, [&](const auto& guid) { return links.at(guid) != 1; });
      if (row.outputs.empty()) row.newerPrefabSource = false;
    }
  }
  if (connection::json::write(ca::encodeProjectScan(scan)).size() > ca::kProjectResultBytes) {
    for (auto& root : scan.roots) {
      incomplete(root, "The scan exceeded its response budget.");
      root.sources.clear();
    }
  }
}
}
