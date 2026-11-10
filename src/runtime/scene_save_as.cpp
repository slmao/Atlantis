#include "scene_save_as.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/guid_mint.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace atlantis::runtime {
namespace {

namespace fs = std::filesystem;
namespace as = asset_system;
namespace ca = connection::authoring;
using Error = ca::SaveAsError;
using CopyResult = Result<ca::SavedScene, Error>;

struct CloseHandleDeleter {
  void operator()(void* value) const { CloseHandle(value); }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

Handle open(const fs::path& path, DWORD access, DWORD sharing, DWORD creation, DWORD flags) {
  HANDLE handle = CreateFileW(path.c_str(), access, sharing, nullptr, creation,
                             flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  return Handle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
}

bool isOrdinary(const Handle& handle, bool directory) {
  BY_HANDLE_FILE_INFORMATION info{};
  return handle && GetFileInformationByHandle(handle.get(), &info) &&
         !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
         ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
         (directory || info.nNumberOfLinks == 1);
}

bool isCaseInsensitive(const Handle& handle) {
  FILE_CASE_SENSITIVE_INFO info{};
  return GetFileInformationByHandleEx(handle.get(), FileCaseSensitiveInfo, &info, sizeof(info)) &&
         !(info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR);
}

std::vector<Handle> pinParents(const fs::path& directory) {
  std::vector<Handle> pins;
  fs::path path = directory.root_path();
  if (!directory.is_absolute() || GetDriveTypeW(path.c_str()) != DRIVE_FIXED) return {};
  for (const auto& part : directory.relative_path()) {
    if (part == L"." || part == L"..") return {};
    path /= part;
    // Deny delete sharing for every parent so validation cannot be bypassed
    // by replacing a checked directory while a copy is being prepared.
    auto pin = open(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS);
    if (!isOrdinary(pin, true) || !isCaseInsensitive(pin)) return {};
    pins.push_back(std::move(pin));
  }
  wchar_t filesystem[16]{};
  if (pins.empty() || !GetVolumeInformationByHandleW(pins.back().get(), nullptr, 0, nullptr, nullptr, nullptr,
                                                     filesystem, 16) ||
      std::wstring_view(filesystem) != L"NTFS") return {};
  return pins;
}

bool writeNew(const fs::path& path, std::string_view contents, bool& created) {
  auto file = open(path, GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL);
  created = file != nullptr;
  if (!created || contents.size() > MAXDWORD) return false;
  const auto size = static_cast<DWORD>(contents.size());
  DWORD written = 0;
  return WriteFile(file.get(), contents.data(), size, &written, nullptr) && written == size &&
         FlushFileBuffers(file.get());
}

struct TemporaryUnit {
  fs::path path;
  Handle directory;
  bool sceneCreated = false;
  bool fragmentCreated = false;
  ~TemporaryUnit() {
    if (path.empty()) return;
    if (sceneCreated) DeleteFileW((path / L"scene.scene.txt").c_str());
    if (fragmentCreated) DeleteFileW((path / L"catalog-source.txt").c_str());
    if (isOrdinary(directory, true)) {
      FILE_DISPOSITION_INFO disposition{TRUE};
      SetFileInformationByHandle(directory.get(), FileDispositionInfo, &disposition, sizeof(disposition));
    } else if (!directory) {
      // Nothing was written before the directory was pinned. Remove only an
      // empty ordinary directory at the name this request just created.
      const DWORD attributes = GetFileAttributesW(path.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
          !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveDirectoryW(path.c_str());
    }
  }
};

struct CommitLock {
  Handle mutex;
  bool acquired = false;
  ~CommitLock() { if (acquired) ReleaseMutex(mutex.get()); }
};

Handle workspaceMutex(const Handle& root) {
  FILE_ID_INFO identity{};
  if (!GetFileInformationByHandleEx(root.get(), FileIdInfo, &identity, sizeof(identity))) return {};
  // The pinned directory identity also unifies different paths to the same workspace.
  auto name = L"Global\\Atlantis.SceneSaveAs." + std::to_wstring(identity.VolumeSerialNumber) + L".";
  constexpr wchar_t hex[] = L"0123456789abcdef";
  for (const auto byte : identity.FileId.Identifier) {
    name += hex[byte >> 4];
    name += hex[byte & 15];
  }
  return Handle(CreateMutexW(nullptr, FALSE, name.c_str()));
}

bool renameNew(const Handle& directory, const fs::path& target) {
  const auto bytes = target.native().size() * sizeof(wchar_t);
  const auto size = offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t);
  auto storage = std::make_unique<std::byte[]>(size);
  auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.get());
  info->ReplaceIfExists = FALSE;
  info->RootDirectory = nullptr;
  info->FileNameLength = static_cast<DWORD>(bytes);
  std::memcpy(info->FileName, target.c_str(), bytes);
  return SetFileInformationByHandle(directory.get(), FileRenameInfo, info, static_cast<DWORD>(size));
}

std::optional<std::string> readCatalog(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(file)), {});
  if (file.bad()) return std::nullopt;
  return text;
}

std::optional<std::string> readFragment(const fs::path& path) {
  auto file = open(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
  if (!isOrdinary(file, false)) return std::nullopt;
  char bytes[4096];
  DWORD size = 0;
  if (!ReadFile(file.get(), bytes, sizeof(bytes), &size, nullptr) || size == sizeof(bytes)) return std::nullopt;
  return std::string(bytes, size);
}

std::optional<Error> checkIdentity(const fs::path& root, const fs::path& catalogSource,
                                   const fs::path& cookedCatalog, const ca::SavedScene& copy) {
  const auto sourceText = readCatalog(catalogSource);
  const auto cookedText = readCatalog(cookedCatalog);
  if (!sourceText || !cookedText) return Error::WriteFailed;
  const auto source = as::parseAssetCatalogSource(*sourceText);
  const auto cooked = as::parseAssetCatalogRecords(*cookedText);
  if (source.isErr() || cooked.isErr()) return Error::WriteFailed;
  const auto key = as::assetKey(copy.scene);
  if (key == 0 || source.value().find(as::CatalogRoot::Assets, copy.source)) return Error::IdentityConflict;
  for (const auto& entry : source.value().entries())
    if (entry.guid == copy.scene || as::assetKey(entry.guid) == key) return Error::IdentityConflict;
  for (const auto& entry : cooked.value())
    if (entry.guid == copy.scene || entry.assetId == key) return Error::IdentityConflict;

  for (const auto& item : fs::directory_iterator(root)) {
    const auto leaf = item.path().filename().wstring();
    if (!leaf.starts_with(L"save_") || !std::ranges::all_of(leaf, [](wchar_t c) { return c <= 0x7f; })) continue;
    std::string siblingName(leaf.size() - 5, '\0');
    std::transform(leaf.begin() + 5, leaf.end(), siblingName.begin(), [](wchar_t c) { return static_cast<char>(c); });
    if (!ca::validSaveAsName(siblingName)) continue;
    auto sibling = open(item.path(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS);
    if (!sibling) return Error::UnsafeTarget;
    if (!isOrdinary(sibling, true)) {
      if (isOrdinary(sibling, false)) continue;
      return Error::UnsafeTarget;
    }
    const auto fragmentPath = item.path() / L"catalog-source.txt";
    const auto attributes = GetFileAttributesW(fragmentPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) continue;
    const auto text = readFragment(fragmentPath);
    if (!text) return Error::UnsafeTarget;
    const auto fragment = as::parseAssetCatalogSource(*text);
    if (fragment.isErr() || fragment.value().entries().size() != 1) return Error::IdentityConflict;
    const auto& entry = fragment.value().entries().front();
    if (entry.type != as::CatalogAssetType::Scene || entry.root != as::CatalogRoot::Assets ||
        entry.path != ca::saveAsSource(siblingName) || entry.guid == copy.scene ||
        as::assetKey(entry.guid) == key) return Error::IdentityConflict;
  }
  return std::nullopt;
}

}  // namespace

bool runningFromSceneDevelopmentBuild(const fs::path& runtimeDirectory) {
  std::wstring executable(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0 || length == executable.size()) return false;
  executable.resize(length);
  std::error_code error;
  return fs::equivalent(fs::path(executable).parent_path(), runtimeDirectory, error) && !error;
}

CopyResult publishSceneCopy(const fs::path& assetsRoot, const fs::path& catalogSource, const fs::path& cookedCatalog,
                            std::string_view name, std::string source, std::uint64_t documentRevision) {
  if (!ca::validSaveAsName(name)) return CopyResult::Err(Error::InvalidName);
  try {
    const auto root = (assetsRoot / L"scenes" / L"saved").make_preferred();
    const auto pins = pinParents(root);
    if (pins.empty()) return CopyResult::Err(Error::UnsafeTarget);
    const auto target = root / ("save_" + std::string(name));
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) return CopyResult::Err(Error::NameConflict);

    const auto ids = as::mintAssetGuids(2);
    ca::SavedScene copy{ids[0], ca::saveAsSource(name), documentRevision};
    auto pending = root / (".pending-" + as::toString(ids[1]));
    if (!CreateDirectoryW(pending.c_str(), nullptr)) return CopyResult::Err(Error::WriteFailed);
    TemporaryUnit temporary{std::move(pending), {}};
    temporary.directory = open(temporary.path, DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    if (!isOrdinary(temporary.directory, true) || !isCaseInsensitive(temporary.directory))
      return CopyResult::Err(Error::UnsafeTarget);
    const auto fragment = as::serializeAssetCatalogSource(
        {{copy.scene, as::CatalogAssetType::Scene, as::CatalogRoot::Assets, copy.source}});
    if (!writeNew(temporary.path / L"scene.scene.txt", source, temporary.sceneCreated) ||
        !writeNew(temporary.path / L"catalog-source.txt", fragment, temporary.fragmentCreated))
      return CopyResult::Err(Error::WriteFailed);

    CommitLock lock{workspaceMutex(pins.back())};
    if (!lock.mutex) return CopyResult::Err(Error::WriteFailed);
    const auto acquired = WaitForSingleObject(lock.mutex.get(), 0);
    if (acquired == WAIT_TIMEOUT) return CopyResult::Err(Error::Busy);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return CopyResult::Err(Error::WriteFailed);
    lock.acquired = true;
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) return CopyResult::Err(Error::NameConflict);
    if (const auto error = checkIdentity(root, catalogSource, cookedCatalog, copy)) return CopyResult::Err(*error);
    if (!renameNew(temporary.directory, target)) {
      const auto error = GetLastError();
      return CopyResult::Err(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ||
                            GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES
                                ? Error::NameConflict : Error::WriteFailed);
    }
    temporary.path.clear();
    return CopyResult::Ok(std::move(copy));
  } catch (const std::exception&) {
    return CopyResult::Err(Error::WriteFailed);
  }
}

}  // namespace atlantis::runtime
