#include "asset_rebuild_host.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <atlantis/connection/json.h>
#include <atlantis/log.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
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

// Validate components before using the path. A missing last component is
// allowed for a stamp that has already been invalidated.
bool safeRelative(const fs::path& root, const fs::path& relative, bool missingLast = false) {
  if (relative.empty() || relative.has_root_path()) return false;
  auto path = root;
  for (auto i = relative.begin(); i != relative.end(); ++i) {
    const auto& name = i->native();
    if (name.empty() || name.back() == L'.' || name.back() == L' ' || name.find(L':') != std::wstring::npos)
      return false;
    path /= *i;
    const bool last = std::next(i) == relative.end();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
      return last && missingLast && GetLastError() == ERROR_FILE_NOT_FOUND;
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
bool matchesIdentity(const as::AssetCatalog& a, const as::AssetCatalog& b) {
  if (a.size() != b.size()) return false;
  for (const auto& record : a.records()) {
    const auto* other = b.find(record.guid);
    if (!other || record.type != other->type || as::toString(record.source) != as::toString(other->source)) return false;
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
json::Value manifest(std::string_view owner, ca::GenerationId id, const Producers& producers) {
  auto value = json::Value::object();
  value.set("profile", json::Value::string(std::string(owner)));
  value.set("generation", json::Value::string(as::toString(id.value)));
  value.set("importedSources", generatedSources(producers));
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
                            Interrupted interrupted) {
  const auto launchFailed = [] { return fail(Stage::Launch, Code::LaunchFailed, "Cannot start contained CMake asset build."); };
  Handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                       static_cast<DWORD>(sizeof(limits)))) return launchFailed();
  SECURITY_ATTRIBUTES inheritable{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE};
  const fs::path logPath = staging / "asset-rebuild.log";
  if (!safeRelative(staging, logPath.filename(), true)) return launchFailed();
  Handle log(CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS, 0, nullptr));
  Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));
  if (!log || !input) return launchFailed();
  std::wstring command = quote(cmake.native()) + L" --build " + quote(staging.native()) + L" --config " +
      quote(fromUtf8(configuration).native()) + L" --target atlantis_asset_catalog";
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
  if (exit != 0) return fail(Stage::Pipeline, Code::BuildFailed, "The asset catalog target failed. See asset-rebuild.log.",
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
}  // namespace

struct AssetRebuildHost::State {
  struct Work {
    ca::RebuildJob job;
    const Producer* producer;
    std::shared_ptr<const as::AssetCatalog> catalog;
    Clock::time_point deadline;
  };

  Config config;
  fs::path staging, publication, active, inventoryPath;
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

  std::optional<ca::RebuildError> admissionLocked() const {
    if (stopping) return ca::RebuildError::Unavailable;
    if (!writer || running) return ca::RebuildError::Busy;
    if (dirty) return ca::RebuildError::WorkspaceNeedsRepair;
    return std::nullopt;
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
    if (ec) return fail(Stage::Launch, Code::LaunchFailed, "Cannot invalidate the producer stamp.");
    if (interrupted()) return timedOut(Stage::Launch);
    if (auto error = runBuild(config.cmake, staging, config.configuration, interrupted)) return *error;
    if (interrupted()) return timedOut(Stage::Validation);
    const auto catalog = readCatalog(staging);
    const auto after = readText(inventoryPath);
    if (!catalog || !after || *after != inventory)
      return fail(Stage::Validation, Code::InvalidOutput, "Staging catalog or producer inventory is invalid.", true);
    if (!matchesIdentity(*work.catalog, *catalog))
      return fail(Stage::Validation, Code::IdentityChanged, "Rebuild changed the catalog identity set.");
    const auto files = outputFiles(staging, *catalog, producers);
    if (!files) return fail(Stage::Validation, Code::InvalidOutput, "Staging output is missing or unsafe.", true);
    if (interrupted()) return timedOut(Stage::Validation);

    const auto guid = newGuid();
    if (!guid) return fail(Stage::Publication, Code::PublishFailed, "Cannot allocate a generation ID.");
    const ca::GenerationId id{*guid};
    const auto name = as::toString(*guid);
    const fs::path temp = publication / (".pending-" + name);
    if (!fs::create_directory(temp, ec) || ec)
      return fail(Stage::Publication, Code::PublishFailed, "Cannot create publication staging.");
    // Interrupted publication trees are unselectable and can be removed offline.
    for (const auto& file : *files) {
      if (!copyOutput(staging / file, temp / file, interrupted))
        return interrupted() ? timedOut(Stage::Publication)
                             : fail(Stage::Publication, Code::PublishFailed, "Cannot copy a complete generation.");
    }
    if (!writeText(temp / kManifest, json::write(manifest(owner, id, producers))))
      return fail(Stage::Publication, Code::PublishFailed, "Cannot write generation manifest.");
    // This lock is independent of job observation. Shutdown and publication
    // settle in order without making the frame thread wait on filesystem I/O.
    std::lock_guard commit(publicationCommit);
    if (interrupted()) return timedOut(Stage::Publication);
    if (!MoveFileExW(temp.c_str(), (publication / name).c_str(), 0))
      return fail(Stage::Publication, Code::PublishFailed, "Cannot publish the generation directory.");
    return id;
  }

  void run() {
    for (;;) {
      std::optional<Work> work;
      {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return stopping || running.has_value(); });
        if (stopping && !running) return;
        work = running;
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
  state->inventoryPath = *stage / inventory;
  if (!safeRelative(seed, inventory) || !safeRelative(*stage, inventory))
    return R::Err("The configured asset producer inventory is missing or unsafe.");
  const auto seedInventory = readText(seed / inventory);
  auto stageInventory = readText(state->inventoryPath);
  if (!seedInventory || !stageInventory || *seedInventory != *stageInventory)
    return R::Err("Staging must use the same checkout, generator, configuration and asset declarations.");
  auto producers = readProducers(*stageInventory);
  if (!producers) return R::Err("Invalid configured asset producer inventory.");
  state->inventory = std::move(*stageInventory);
  state->producers = std::move(*producers);
  const std::string buildIdentity = state->inventory.substr(0, state->inventory.find('\n'));
  auto binding = json::Value::object();
  binding.set("seed", json::Value::string(utf8(seed)));
  binding.set("staging", json::Value::string(utf8(*stage)));
  binding.set("configuration", json::Value::string(state->config.configuration));
  binding.set("build", json::Value::string(buildIdentity));
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
    const auto* configuration = recorded ? recorded->find("configuration") : nullptr;
    const auto* build = recorded ? recorded->find("build") : nullptr;
    if (!oldSeed || !oldStage || !fs::equivalent(seed, *oldSeed, ec) || ec ||
        !fs::equivalent(*stage, *oldStage, ec) || ec || !configuration || !configuration->isString() ||
        configuration->asString() != state->config.configuration || !build || !build->isString() ||
        build->asString() != buildIdentity)
      return R::Err("Publication root belongs to a different profile.");
    state->owner = json::write(*recorded);
  }
  state->active = seed / kCatalog;
  if (const auto generation = state->config.generation) {
    const fs::path selected = *publication / as::toString(generation->value);
    if (!realDirectory(selected) || !safeRelative(selected, fs::path(kManifest)) ||
        readJson(selected / kManifest) != manifest(state->owner, *generation, state->producers))
      return R::Err("Unknown, incomplete or foreign asset generation.");
    const auto catalog = readCatalog(selected);
    if (!catalog || !outputFiles(selected, *catalog, state->producers))
      return R::Err("Incomplete asset generation files.");
    state->active = selected / kCatalog;
  }
  const fs::path lock = *stage / ".atlantis-rebuild-lock";
  if (!safeRelative(*stage, lock.filename(), true)) return R::Err("Unsafe workspace writer lock path.");
  state->writer = Handle(CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr));
  if (!state->writer && GetLastError() != ERROR_SHARING_VIOLATION) return R::Err("Cannot open the workspace writer lock.");
  if (state->writer) {
    state->dirty = !safeRelative(*stage, fs::path(kDirty), true) || fs::exists(*stage / kDirty, ec) || static_cast<bool>(ec);
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
}  // namespace atlantis::runtime
