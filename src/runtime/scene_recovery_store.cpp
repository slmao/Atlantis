#include "scene_recovery_store.h"

#include <atlantis/asset_system/guid_mint.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace atlantis::runtime {
namespace {

namespace fs = std::filesystem;
namespace ac = connection::authoring;
namespace as = asset_system;
using Snapshot = authoring::RecoverySnapshot;
using Error = ac::RecoveryError;
constexpr std::size_t kEnvelopeOverhead = 1024;
constexpr std::string_view kSignature = "atlantis.scene-recovery/1";

struct CloseFile {
  void operator()(void* handle) const { CloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseFile>;

Handle openFile(const fs::path& path, DWORD access, DWORD share, DWORD mode,
                DWORD flags = FILE_ATTRIBUTE_NORMAL) {
  HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, mode,
                         flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  return Handle(h == INVALID_HANDLE_VALUE ? nullptr : h);
}

bool ordinary(const Handle& handle, bool directory) {
  BY_HANDLE_FILE_INFORMATION info{};
  return handle && GetFileInformationByHandle(handle.get(), &info) &&
         !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
         ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
         (directory || info.nNumberOfLinks == 1);
}

std::vector<Handle> pinParents(const fs::path& directory, bool create) {
  std::vector<Handle> pins;
  fs::path path = directory.root_path();
  if (!directory.is_absolute() || GetDriveTypeW(path.c_str()) != DRIVE_FIXED) return {};
  for (const auto& part : directory.relative_path()) {
    if (part == L"." || part == L".." || part.empty()) return {};
    path /= part;
    if (create && !CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    auto pin = openFile(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS);
    FILE_CASE_SENSITIVE_INFO caseInfo{};
    if (!ordinary(pin, true) ||
        !GetFileInformationByHandleEx(pin.get(), FileCaseSensitiveInfo, &caseInfo, sizeof(caseInfo)) ||
        (caseInfo.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR)) return {};
    pins.push_back(std::move(pin));
  }
  wchar_t filesystem[16]{};
  if (pins.empty() || !GetVolumeInformationByHandleW(pins.back().get(), nullptr, 0, nullptr, nullptr, nullptr,
                                                     filesystem, 16) ||
      std::wstring_view(filesystem) != L"NTFS") return {};
  return pins;
}

std::optional<std::wstring> canonical(const Handle& file) {
  std::wstring name(32768, L'\0');
  const DWORD length = GetFinalPathNameByHandleW(file.get(), name.data(), static_cast<DWORD>(name.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
  if (length == 0 || length >= name.size()) return std::nullopt;
  name.resize(length);
  const int size = static_cast<int>(name.size());
  const int foldedSize = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, name.data(), size,
                                       nullptr, 0, nullptr, nullptr, 0);
  if (foldedSize <= 0) return std::nullopt;
  std::wstring folded(static_cast<std::size_t>(foldedSize), L'\0');
  if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, name.data(), size, folded.data(),
                    foldedSize, nullptr, nullptr, 0) != foldedSize) return std::nullopt;
  return folded;
}

std::optional<std::string> sha(std::string_view bytes) {
  if (bytes.size() > ac::kRecoveryMaxSourceBytes + kEnvelopeOverhead) return std::nullopt;
  BCRYPT_ALG_HANDLE provider{};
  if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return std::nullopt;
  std::array<UCHAR, 32> value{};
  const auto result = BCryptHash(provider, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
      value.data(), static_cast<ULONG>(value.size()));
  BCryptCloseAlgorithmProvider(provider, 0);
  if (result < 0) return std::nullopt;
  std::string hex;
  hex.reserve(value.size() * 2);
  for (const auto byte : value) {
    constexpr char digits[] = "0123456789abcdef";
    hex += digits[byte >> 4];
    hex += digits[byte & 15];
  }
  return hex;
}

std::string_view wideBytes(const std::wstring& text) {
  return {reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t)};
}

std::optional<std::string> readAll(const Handle& file, std::size_t limit) {
  if (!ordinary(file, false)) return std::nullopt;
  LARGE_INTEGER length{};
  if (!GetFileSizeEx(file.get(), &length) || length.QuadPart < 0 ||
      static_cast<std::uint64_t>(length.QuadPart) > limit) return std::nullopt;
  std::string bytes(static_cast<std::size_t>(length.QuadPart), '\0');
  DWORD read = 0;
  if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
      read != bytes.size()) return std::nullopt;
  return bytes;
}

bool removeOnClose(const Handle& file) {
  FILE_DISPOSITION_INFO info{TRUE};
  return SetFileInformationByHandle(file.get(), FileDispositionInfo, &info, sizeof(info)) != 0;
}

bool replaceSlot(const Handle& file, const fs::path& path) {
  const auto bytes = path.native().size() * sizeof(wchar_t);
  const auto size = offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t);
  auto storage = std::make_unique<std::byte[]>(size);
  auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.get());
  info->ReplaceIfExists = TRUE;
  info->RootDirectory = nullptr;
  info->FileNameLength = static_cast<DWORD>(bytes);
  std::memcpy(info->FileName, path.c_str(), bytes);
  return SetFileInformationByHandle(file.get(), FileRenameInfo, info, static_cast<DWORD>(size)) != 0;
}

struct SlotIdentity {
  DWORD volume, highId, lowId, highSize, lowSize;
  DWORD highWriteTime, lowWriteTime;
  friend bool operator==(const SlotIdentity&, const SlotIdentity&) = default;
};

std::optional<SlotIdentity> identity(const Handle& file) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!ordinary(file, false) || !GetFileInformationByHandle(file.get(), &info)) return std::nullopt;
  return SlotIdentity{info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
    info.nFileSizeHigh, info.nFileSizeLow, info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime};
}

bool absent(DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

std::optional<std::uint64_t> unsignedWord(std::string_view text) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value == 0 || std::to_string(value) != text) return std::nullopt;
  return value;
}

bool digestWord(std::string_view text) {
  return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::optional<Snapshot> decode(std::string_view bytes, std::string_view key, const as::AssetGuid& scene) {
  std::array<std::string_view, 9> header;
  for (auto& word : header) {
    const auto end = bytes.find('\n');
    if (end == std::string_view::npos || end > 128) return std::nullopt;
    word = bytes.substr(0, end);
    bytes.remove_prefix(end + 1);
  }
  if (header[0] != kSignature || header[1] != key || header[2] != as::toString(scene)) return std::nullopt;
  const auto id = as::parseEntityGuid(header[3]);
  const auto revision = unsignedWord(header[4]);
  const auto time = unsignedWord(header[5]);
  const auto size = unsignedWord(header[8]);
  if (id.isErr() || !revision || !time || *time > 253402300799ull ||
      !size || *size != bytes.size() || *size > ac::kRecoveryMaxSourceBytes ||
      !digestWord(header[6]) || !digestWord(header[7]) || sha(bytes) != header[7]) return std::nullopt;
  return Snapshot{{id.value(), *revision, *time, true}, std::string(header[6]), std::string(bytes)};
}

struct Prepared {
  Handle file;
  ac::RecoveryCopy copy;
  std::string seal;
  std::uint64_t epoch = 0;
  ~Prepared() { if (file) (void)removeOnClose(file); }
};

}  // namespace

struct SceneRecoveryStore::State {
  State(fs::path path, as::AssetGuid guid) : source(std::move(path)), scene(guid) {}
  ~State() {
    {
      std::lock_guard lock(mutex);
      stop = true;
    }
    ready.notify_all();
    if (worker.joinable()) worker.join();
    prepared.reset();
    if (leased) ReleaseMutex(lease.get());
  }

  struct Work {
    Snapshot snapshot;
    std::uint64_t epoch;
  };

  fs::path source;
  as::AssetGuid scene;
  std::wstring sourceIdentity;
  std::vector<Handle> sourcePins, storePins;
  Handle lease;
  bool leased = false;
  bool initialized = false;
  bool invalid = false;
  std::string key;
  fs::path directory, slot;
  std::optional<SlotIdentity> slotIdentity;
  std::optional<ac::RecoveryCopy> copy;
  std::optional<std::uint64_t> protectedRevision;
  std::string seal;
  ac::RecoveryCapture capture = ac::RecoveryCapture::Unavailable;
  Error unavailable = Error::Unavailable;
  std::uint64_t epoch = 0;

  std::thread worker;
  std::mutex mutex;
  std::condition_variable ready;
  std::optional<Work> requested;
  std::unique_ptr<Prepared> prepared;
  bool stop = false;

  bool initialize() {
    if (initialized) return leased;
    const auto absolute = source.lexically_normal();
    sourcePins = pinParents(absolute.parent_path(), false);
    if (sourcePins.empty()) return false;
    const auto sourceFile = openFile(absolute, FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING);
    if (!ordinary(sourceFile, false)) return false;
    auto sourceName = canonical(sourceFile);
    if (!sourceName) return false;
    sourceIdentity = std::move(*sourceName);
    auto slotKey = sha(std::string(wideBytes(sourceIdentity)) + as::toString(scene));
    if (!slotKey) return false;
    key = std::move(*slotKey);
    std::wstring profile(32768, L'\0');
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", profile.data(), static_cast<DWORD>(profile.size()));
    if (count == 0 || count >= profile.size()) return false;
    profile.resize(count);
    directory = fs::path(profile) / L"Atlantis" / L"SceneRecovery" / L"v1";
    storePins = pinParents(directory, true);
    if (storePins.empty()) return false;
    const auto storeName = canonical(storePins.back());
    const auto leaseKey = storeName ? sha(std::string(wideBytes(*storeName)) + key) : std::nullopt;
    if (!leaseKey) return false;
    const auto mutexName = L"Global\\Atlantis.SceneRecovery." + std::wstring(leaseKey->begin(), leaseKey->end());
    lease = Handle(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
    if (!lease) return false;
    const DWORD result = WaitForSingleObject(lease.get(), 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
      unavailable = result == WAIT_TIMEOUT ? Error::Busy : Error::Unavailable;
      return false;
    }
    leased = initialized = true;
    slot = directory / (key + ".recovery");
    const auto file = openFile(slot, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    const DWORD openError = file ? ERROR_SUCCESS : GetLastError();
    if (file) {
      const auto bytes = readAll(file, ac::kRecoveryMaxSourceBytes + kEnvelopeOverhead);
      const auto snapshot = bytes ? decode(*bytes, key, scene) : std::nullopt;
      const auto digest = bytes ? sha(*bytes) : std::nullopt;
      slotIdentity = identity(file);
      if (snapshot && digest && slotIdentity) {
        copy = snapshot->copy;
        seal = *digest;
      } else invalid = true;
    } else if (!absent(openError)) invalid = true;
    capture = invalid ? ac::RecoveryCapture::Failed : ac::RecoveryCapture::Idle;
    cleanAbandonedFiles();
    return true;
  }

  void cleanAbandonedFiles() {
    const std::string prefix = key + ".tmp-";
    const auto glob = directory / (prefix + "*");
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW(glob.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
      const fs::path path = directory / data.cFileName;
      const auto name = path.filename().u8string();
      const std::string utf8(name.begin(), name.end());
      if (!utf8.starts_with(prefix) || as::parseEntityGuid(std::string_view(utf8).substr(prefix.size())).isErr()) continue;
      auto target = openFile(path, DELETE | FILE_READ_ATTRIBUTES, 0, OPEN_EXISTING);
      if (ordinary(target, false)) (void)removeOnClose(target);
    } while (FindNextFileW(search, &data));
    FindClose(search);
  }

  void invalidate() {
    ++epoch;
    std::unique_ptr<Prepared> stale;
    {
      std::lock_guard lock(mutex);
      requested.reset();
      stale = std::move(prepared);
    }
    ready.notify_all();
  }

  [[nodiscard]] std::optional<std::string> verifiedBytes(const Handle& file) const {
    if (identity(file) != slotIdentity) return std::nullopt;
    auto bytes = readAll(file, ac::kRecoveryMaxSourceBytes + kEnvelopeOverhead);
    if (!bytes || sha(*bytes) != seal) return std::nullopt;
    return bytes;
  }

  [[nodiscard]] bool unlink() {
    const auto file = openFile(slot, GENERIC_READ | DELETE, 0, OPEN_EXISTING);
    if (!verifiedBytes(file) || !removeOnClose(file)) return false;
    copy.reset();
    slotIdentity.reset();
    seal.clear();
    return true;
  }

  void run() {
    for (;;) {
      std::optional<Work> work;
      {
        std::unique_lock lock(mutex);
        ready.wait(lock, [&] { return stop || (requested && !prepared); });
        if (stop) return;
        work.swap(requested);
      }
      auto next = std::make_unique<Prepared>();
      next->epoch = work->epoch;
      next->copy = work->snapshot.copy;
      const auto& payload = work->snapshot.payload;
      if (const auto digest = sha(payload)) {
        const std::string contents = std::string(kSignature) + "\n" + key + "\n" + as::toString(scene) + "\n" +
            as::toString(next->copy.id) + "\n" + std::to_string(next->copy.revision) + "\n" +
            std::to_string(next->copy.capturedUnixSeconds) + "\n" + work->snapshot.baseDigest + "\n" +
            *digest + "\n" + std::to_string(payload.size()) + "\n" + payload;
        const auto name = directory / (key + ".tmp-" + as::toString(next->copy.id));
        auto file = openFile(name, GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES, 0, CREATE_NEW);
        if (file) {
          DWORD wrote = 0;
          const auto length = static_cast<DWORD>(contents.size());
          const auto digestOfEnvelope = sha(contents);
          if (digestOfEnvelope && WriteFile(file.get(), contents.data(), length, &wrote, nullptr) &&
              wrote == length && FlushFileBuffers(file.get())) {
            // Keep the ordinary file sealed against writes and path substitution until the host publishes it.
            next->file = std::move(file);
            next->seal = *digestOfEnvelope;
          } else (void)removeOnClose(file);
        }
      }
      std::lock_guard lock(mutex);
      prepared = std::move(next);
    }
  }
};

SceneRecoveryStore::SceneRecoveryStore(fs::path source, as::AssetGuid scene)
    : state_(std::make_unique<State>(std::move(source), scene)) {}
SceneRecoveryStore::~SceneRecoveryStore() = default;

void SceneRecoveryStore::opened() {
  auto& s = *state_;
  if (!s.initialize()) return;
  s.invalidate();
  s.protectedRevision.reset();
  if (s.copy) s.copy->pending = true;
  if (!s.invalid && s.capture != ac::RecoveryCapture::Failed) s.capture = ac::RecoveryCapture::Idle;
}

std::optional<std::string> SceneRecoveryStore::fingerprint(std::string_view bytes) const {
  if (bytes.size() > ac::kRecoveryMaxSourceBytes) return std::nullopt;
  return sha(bytes);
}

std::optional<std::string> SceneRecoveryStore::readSource() const {
  const auto& s = *state_;
  if (!s.leased) return std::nullopt;
  // Deny writes and replacement for the duration of the read.
  const auto file = openFile(s.source, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
  if (!ordinary(file, false) || canonical(file) != s.sourceIdentity) return std::nullopt;
  return readAll(file, ac::kRecoveryMaxSourceBytes);
}

ac::RecoveryStatus SceneRecoveryStore::status() const {
  const auto& s = *state_;
  return {s.capture, s.protectedRevision, s.copy, {}};
}

void SceneRecoveryStore::capture(std::uint64_t revision, std::string baseDigest, std::string payload) {
  auto& s = *state_;
  if (!s.leased || s.invalid || (s.copy && s.copy->pending)) return;
  if (!digestWord(baseDigest) || payload.empty() || payload.size() > ac::kRecoveryMaxSourceBytes) {
    s.invalidate();
    s.capture = ac::RecoveryCapture::Failed;
    return;
  }
  using namespace std::chrono;
  const auto time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  const ac::RecoveryCopy copy{as::mintEntityGuid(), revision, static_cast<std::uint64_t>(time), false};
  if (!s.worker.joinable()) {
    try { s.worker = std::thread([&s] { s.run(); }); }
    catch (const std::system_error&) { s.capture = ac::RecoveryCapture::Failed; return; }
  }
  s.capture = ac::RecoveryCapture::Writing;
  {
    std::lock_guard lock(s.mutex);
    s.requested = State::Work{{copy, std::move(baseDigest), std::move(payload)}, s.epoch};
  }
  s.ready.notify_one();
}

void SceneRecoveryStore::settle() {
  auto& s = *state_;
  if (!s.leased || !s.worker.joinable()) return;
  std::unique_ptr<Prepared> item;
  bool more = false;
  {
    std::lock_guard lock(s.mutex);
    item = std::move(s.prepared);
    more = s.requested.has_value();
  }
  if (!item) return;
  s.ready.notify_one();
  if (item->epoch != s.epoch) return;
  if (!item->file) { s.capture = more ? ac::RecoveryCapture::Writing : ac::RecoveryCapture::Failed; return; }
  {
    auto current = openFile(s.slot, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE, OPEN_EXISTING);
    const DWORD openError = current ? ERROR_SUCCESS : GetLastError();
    if ((s.slotIdentity && identity(current) != s.slotIdentity) ||
        (!s.slotIdentity && (current || !absent(openError)))) {
      s.invalid = true;
      s.protectedRevision.reset();
      s.capture = ac::RecoveryCapture::Failed;
      return;
    }
    // A Windows rename over an open destination can fail even with delete
    // sharing. Release our metadata handle under the exclusive recovery lease.
    current.reset();
    if (!replaceSlot(item->file, s.slot)) {
      s.capture = more ? ac::RecoveryCapture::Writing : ac::RecoveryCapture::Failed;
      return;
    }
  }
  item->file.reset();
  const auto file = openFile(s.slot, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, OPEN_EXISTING);
  s.slotIdentity = identity(file);
  if (!s.slotIdentity) {
    s.invalid = true;
    s.capture = ac::RecoveryCapture::Failed;
    return;
  }
  s.copy = item->copy;
  s.seal = std::move(item->seal);
  s.protectedRevision = item->copy.revision;
  s.capture = more ? ac::RecoveryCapture::Writing : ac::RecoveryCapture::Idle;
}

void SceneRecoveryStore::retire() {
  auto& s = *state_;
  if (!s.leased || s.invalid || (s.copy && s.copy->pending)) return;
  s.invalidate();
  s.protectedRevision.reset();
  s.capture = !s.copy || s.unlink() ? ac::RecoveryCapture::Idle : ac::RecoveryCapture::Failed;
}

Result<Snapshot, Error> SceneRecoveryStore::pending(const as::EntityGuid& id) const {
  using R = Result<Snapshot, Error>;
  const auto& s = *state_;
  if (!s.leased) return R::Err(s.unavailable);
  if (s.invalid) return R::Err(Error::InvalidCopy);
  if (!s.copy || !s.copy->pending || s.copy->id != id) return R::Err(Error::StaleCopy);
  const auto handle = openFile(s.slot, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
  const auto bytes = s.verifiedBytes(handle);
  if (!bytes) return R::Err(Error::StaleCopy);
  if (auto snapshot = decode(*bytes, s.key, s.scene)) return R::Ok(std::move(*snapshot));
  return R::Err(Error::InvalidCopy);
}

void SceneRecoveryStore::adopted(std::uint64_t revision) {
  auto& s = *state_;
  s.invalidate();
  if (s.copy) s.copy->pending = false;
  s.protectedRevision = revision;
}

std::optional<Error> SceneRecoveryStore::discard(const as::EntityGuid& id) {
  auto& s = *state_;
  if (!s.leased) return s.unavailable;
  if (s.invalid) return Error::InvalidCopy;
  if (!s.copy || !s.copy->pending || s.copy->id != id) return Error::StaleCopy;
  const auto handle = openFile(s.slot, GENERIC_READ | DELETE, 0, OPEN_EXISTING);
  if (!handle) return absent(GetLastError()) ? Error::StaleCopy : Error::IoFailed;
  if (!s.verifiedBytes(handle)) return Error::StaleCopy;
  if (!removeOnClose(handle)) return Error::IoFailed;
  s.invalidate();
  s.copy.reset();
  s.protectedRevision.reset();
  s.slotIdentity.reset();
  s.seal.clear();
  s.capture = ac::RecoveryCapture::Idle;
  return std::nullopt;
}

}  // namespace atlantis::runtime
