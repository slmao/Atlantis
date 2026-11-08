#include "asset_rebuild_host.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
#include <sstream>
#include <system_error>
#include <thread>
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
  std::erase_if(producers, [](const auto& row) { return managedName(row.first).has_value(); });
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
                     const fs::path& managedInventory = {}) {
  auto value = json::Value::object();
  value.set("profile", json::Value::string(std::string(owner)));
  value.set("generation", json::Value::string(as::toString(id.value)));
  value.set("importedSources", generatedSources(producers));
  if (!managedInventory.empty()) {
    value.set("version", json::Value::number(std::uint64_t{2}));
    value.set("catalogSource", json::Value::string(std::string(kManagedSource)));
    value.set("producers", json::Value::string(utf8(managedInventory)));
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
bool copyInbox(const fs::path& from, const fs::path& to, Interrupted interrupted) {
  Handle input(CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  constexpr DWORD maxSize = 16 * 1024 * 1024;
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

  Config config;
  fs::path staging, publication, active, inventoryPath, inventoryRelative, project;
  Handle projectWriter;
  Registrations registered, activeRegistered;
  std::optional<ImportWork> importing;
  std::deque<ca::ImportJob> imported;
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
    if (!writer || (!project.empty() && !projectWriter) || running || importing) return ca::RebuildError::Busy;
    if (dirty) return ca::RebuildError::WorkspaceNeedsRepair;
    return std::nullopt;
  }

  std::optional<ca::RebuildError> admissionLocked() const {
    if (auto error = slotError()) return error;
    if (!project.empty() && registered != activeRegistered) return ca::RebuildError::Unavailable;
    return std::nullopt;
  }

  struct Prepared { ca::GenerationId id; fs::path directory; };
  template<class Interrupted>
  std::variant<Prepared, End> prepare(const as::AssetCatalog& catalog, const Producers& view, Interrupted interrupted) {
    auto files = outputFiles(staging, catalog, view);
    if (!files) return fail(Stage::Validation, Code::InvalidOutput, "Staging output is missing or unsafe.", true);
    if (!project.empty()) {
      files->push_back(inventoryRelative);
      files->push_back(fs::path(kManagedSource));
    }
    if (interrupted()) return timedOut(Stage::Validation);
    const auto guid = newGuid();
    if (!guid) return fail(Stage::Publication, Code::PublishFailed, "Cannot allocate a generation ID.");
    const ca::GenerationId id{*guid};
    const fs::path temp = publication / (".pending-" + as::toString(*guid));
    std::error_code ec;
    if (!fs::create_directory(temp, ec) || ec)
      return fail(Stage::Publication, Code::PublishFailed, "Cannot create publication staging.");
    for (const auto& file : *files) {
      if (!copyOutput(staging / file, temp / file, interrupted))
        return interrupted() ? timedOut(Stage::Publication)
                             : fail(Stage::Publication, Code::PublishFailed, "Cannot copy a complete generation.");
    }
    const auto data = manifest(owner, id, view, project.empty() ? fs::path{} : inventoryRelative);
    if (!writeText(temp / kManifest, json::write(data)))
      return fail(Stage::Publication, Code::PublishFailed, "Cannot write generation manifest.");
    return Prepared{id, temp};
  }

  bool publish(const Prepared& generation) const {
    return MoveFileExW(generation.directory.c_str(),
                      (publication / as::toString(generation.id.value)).c_str(), 0);
  }

  std::variant<ca::GenerationId, End> build(const Work& work) {
    const auto interrupted = [&] { return stopping || Clock::now() >= work.deadline; };
    if (interrupted()) return timedOut(Stage::Launch);
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
        inventoryHeader(*after) != inventoryHeader(inventory) || *view != producers))
      return fail(Stage::Validation, Code::InvalidOutput, "Staging catalog or producer inventory is invalid.", true);
    if (work.catalog->size() != catalog->size() || !retainsIdentity(*work.catalog, *catalog))
      return fail(Stage::Validation, Code::IdentityChanged, "Rebuild changed the catalog identity set.");
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
    auto error = runBuild(config.cmake, staging, config.configuration, interrupted, true);
    if (!error) error = runBuild(config.cmake, staging, config.configuration, interrupted);
    if (error) {
      const auto code = error->failure.code == Code::TimedOut ? IC::TimedOut :
                        error->failure.code == Code::LaunchFailed ? IC::LaunchFailed : IC::BuildFailed;
      return failed(IS::Pipeline, code, error->failure.diagnostic, error->repair, error->failure.exitCode);
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
    if (baseProducers(*view) != baseProducers(producers) || managedRegistrations(*view) != expected ||
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

  void run() {
    for (;;) {
      std::optional<Work> work;
      std::optional<ImportWork> import;
      {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return stopping || running || importing; });
        if (stopping && !running && !importing) return;
        work = running;
        import = importing;
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
  if (const auto* optIn = profile->find("importProject")) {
    const auto project = canonicalDirectory(optIn);
    if (!project || !safeProjectRoot(fromUtf8(optIn->asString()), *project) ||
        overlap(*project, seed) || overlap(*project, *stage) || overlap(*project, *publication))
      return R::Err("Import project must be an existing disjoint directory.");
    state->project = *project;
    for (const auto& tree : {seed, *stage}) {
      if (!safeRelative(tree, fs::path(kManagedProject)))
        return R::Err("Both builds must be configured with the import project.");
      const auto configured = readText(tree / kManagedProject);
      if (!configured || !fs::equivalent(fromUtf8(*configured), *project, ec) || ec)
        return R::Err("Both builds must use the configured import project.");
    }
  }
  if (!safeRelative(seed, inventory) || !safeRelative(*stage, inventory))
    return R::Err("The configured asset producer inventory is missing or unsafe.");
  const auto seedInventory = readText(seed / inventory);
  auto stageInventory = readText(state->inventoryPath);
  if (!seedInventory || !stageInventory)
    return R::Err("Cannot read the configured producer inventories.");
  auto producers = readProducers(*stageInventory);
  if (!producers) return R::Err("Invalid configured asset producer inventory.");
  if (state->project.empty()) {
    if (*seedInventory != *stageInventory || !managedRegistrations(*producers).empty())
      return R::Err("Staging must use the same checkout, generator, configuration and asset declarations.");
  } else {
    const auto seedHeader = columns(std::string(inventoryHeader(*seedInventory)));
    if (seedHeader.size() < 2 || overlap(state->project, fromUtf8(seedHeader[1])))
      return R::Err("Import project must be disjoint from the checkout.");
    auto seedView = readManagedView(seed, inventory);
    if (!seedView || inventoryHeader(*seedInventory) != inventoryHeader(*stageInventory) ||
        baseProducers(*seedView) != baseProducers(*producers))
      return R::Err("Managed staging must match the seed's base producers and build configuration.");
    producers = std::move(seedView);
  }
  state->inventory = std::move(*stageInventory);
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
    if (!state->project.empty()) {
      auto frozen = readManagedView(selected, inventory);
      const auto frozenInventory = readText(selected / inventory);
      if (!frozen || !frozenInventory || inventoryHeader(*frozenInventory) != inventoryHeader(state->inventory) ||
          baseProducers(*frozen) != baseProducers(state->producers) || !matchesManagedCatalog(*catalog, *frozen))
        return R::Err("Invalid managed generation metadata.");
      state->producers = std::move(*frozen);
    }
    const auto managedInventory = state->project.empty() ? fs::path{} : inventory;
    if (*selectedManifest != manifest(state->owner, *generation, state->producers, managedInventory))
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
      if (!catalog || !matchesManagedCatalog(*catalog, state->producers))
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
    state->dirty = !safeRelative(*stage, fs::path(kDirty), true) || fs::exists(*stage / kDirty, ec) || static_cast<bool>(ec);
    if (!state->project.empty()) {
      auto registered = readRegistrations(state->project);
      const auto pending = safeRelative(*stage, fs::path(kPendingSelection))
          ? readText(*stage / kPendingSelection) : std::nullopt;
      state->dirty = state->dirty || !registered ||
          !safeRelative(state->project, fs::path(kProjectDirty), true) ||
          fs::exists(state->project / kProjectDirty, ec) || ec || !pending || !pending->empty();
      if (registered) state->registered = std::move(*registered);
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
    if (state_->registered != state_->activeRegistered) return R::Err(E::ActivationRequired);
  } else {
    if (committed == state_->registered.end() || state_->activeRegistered.contains(name))
      return R::Err(E::NothingToResume);
    auto expected = state_->activeRegistered;
    expected.emplace(committed->first, committed->second);
    if (expected != state_->registered) return R::Err(E::ActivationRequired);
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
}  // namespace atlantis::runtime
