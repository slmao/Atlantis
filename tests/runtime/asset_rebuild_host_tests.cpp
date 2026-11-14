#include "../../src/runtime/asset_rebuild_host.h"
#include "../../src/runtime/material_authoring.h"

#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/material_artifact.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
namespace ca = atlantis::connection::assets;
using atlantis::runtime::AssetRebuildHost;
using namespace std::chrono_literals;

void write(const fs::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  out.close();
  REQUIRE(out.good());
}
std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
std::wstring quoted(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }

struct Fixture {
  static inline std::atomic<unsigned> next = 0;
  fs::path base = fs::temp_directory_path() / ("atlantis-rebuild-test-" + std::to_string(GetCurrentProcessId()) +
                                               "-" + std::to_string(next++));
  fs::path seed = base / "seed", staging = base / "staging", published = base / "published";
  fs::path cmake = ATLANTIS_REBUILD_TEST_CMAKE;
  as::AssetGuid guid = as::parseAssetGuid("00850085-0085-4085-8085-008500850085").value();
  as::AssetGuid unknown = as::deriveAssetGuid(guid, "unknown");
  std::shared_ptr<const as::AssetCatalog> snapshot;
  fs::path profile = base / "profile.json";
  fs::path marker = staging / ".atlantis-rebuild-dirty";

  explicit Fixture(bool seedStaging = true) {
    REQUIRE(fs::create_directories(seed / "assets"));
    REQUIRE(fs::create_directories(staging / "assets"));
    REQUIRE(fs::create_directories(published));
    const std::string inventory = "atlantis.asset-producers/1\tfixture\nmaterial\tassets\ta.material.txt\t" +
        as::toString(guid) + "\tassets/materials/material.stamp\t-\n";
    for (const auto& dir : {seed, staging}) {
      write(dir / "assets/producers-Debug.txt", inventory);
      if (dir == staging && !seedStaging) continue;
      write(dir / "artifact", "old");
      write(dir / "metadata", "fixture");
      catalog(dir, guid);
    }
    writeProfile(profile, staging, published);
    auto loaded = as::loadAssetCatalog((seed / "asset_catalog.txt").generic_string());
    REQUIRE(loaded.isOk());
    snapshot = std::make_shared<const as::AssetCatalog>(std::move(loaded.value()));
    write(base / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.28)
project(rebuild_fixture LANGUAGES NONE)
add_custom_target(atlantis_asset_catalog
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/producer.cmake"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" VERBATIM)
)");
    write(base / "producer.cmake", R"(file(READ "mode" mode)
if(EXISTS "assets/materials/material.stamp")
  message(FATAL_ERROR "producer was not forced")
endif()
if(mode STREQUAL "fail")
  message(FATAL_ERROR "intentional tool failure")
elseif(mode STREQUAL "slow")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 8)
endif()
if(NOT EXISTS "asset_catalog.txt")
  file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/seed/asset_catalog.txt" "asset_catalog.txt")
  file(WRITE "metadata" "fixture")
endif()
file(WRITE "artifact" "new")
file(MAKE_DIRECTORY "assets/materials")
file(WRITE "assets/materials/material.stamp" "fixture stamp")
)");
    mode("ok");
    auto cmd = quoted(cmake) + L" -S " + quoted(base) + L" -B " + quoted(staging);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    REQUIRE(CreateProcessW(cmake.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, base.c_str(), &startup, &process));
    CloseHandle(process.hThread);
    const auto waited = WaitForSingleObject(process.hProcess, 20000);
    DWORD exit = 1;
    if (waited == WAIT_TIMEOUT) {
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, INFINITE);
    } else {
      GetExitCodeProcess(process.hProcess, &exit);
    }
    CloseHandle(process.hProcess);
    REQUIRE(waited == WAIT_OBJECT_0);
    REQUIRE(exit == 0);
  }

  ~Fixture() {
    std::error_code ec;
    const auto root = fs::weakly_canonical(base, ec);
    if (!ec && root.parent_path() == fs::weakly_canonical(fs::temp_directory_path(), ec) &&
        root.filename().string().starts_with("atlantis-rebuild-test-")) fs::remove_all(root, ec);
  }
  static void writeProfile(const fs::path& path, const fs::path& stage, const fs::path& publication) {
    write(path, "{\"stagingBuild\":\"" + stage.generic_string() + "\",\"publicationRoot\":\"" +
                 publication.generic_string() + "\"}");
  }
  void catalog(const fs::path& root, as::AssetGuid id) const {
    as::AssetCatalogRecord record;
    record.guid = id;
    record.assetId = as::assetKey(id);
    record.type = as::CatalogAssetType::Material;
    record.source = {as::CatalogRoot::Assets, "a.material.txt", {}};
    record.artifact = "artifact";
    record.metadata = "metadata";
    record.artifactSchema = 1;
    record.tool = "fixture/1";
    write(root / "asset_catalog.txt", as::serializeAssetCatalog({record}));
  }
  void mode(std::string_view name) const { write(staging / "mode", name); }
  AssetRebuildHost::Config config() const { return {profile, seed / "asset_catalog.txt", cmake, "Debug", {}, 15s}; }
  std::unique_ptr<AssetRebuildHost> host() const {
    auto opened = AssetRebuildHost::open(config());
    INFO((opened.isErr() ? opened.error() : std::string{}));
    REQUIRE(opened.isOk());
    return std::move(opened.value());
  }
  ca::RebuildJob complete(const AssetRebuildHost& host, ca::RebuildId id) const {
    const auto limit = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < limit) {
      const auto status = host.status(id);
      REQUIRE(status.isOk());
      REQUIRE(status.value().job);
      if (status.value().job->state != ca::RebuildState::Running) return *status.value().job;
      std::this_thread::sleep_for(20ms);
    }
    FAIL("asset job did not settle");
    return {};
  }
};
}

TEST_CASE("asset rebuild: one canonical writer, explicit publication and activation", "[runtime][asset-rebuild]") {
  Fixture f;
  auto host = f.host();
  auto alias = f.config();
  alias.profile = f.base / "alias.json";
  Fixture::writeProfile(alias.profile, f.staging / ".." / "staging", f.published);
  auto reader = AssetRebuildHost::open(alias);
  REQUIRE(reader.isOk());
  CHECK(reader.value()->start(f.guid, f.snapshot).error() == ca::RebuildError::Busy);
  CHECK(host->start(f.unknown, f.snapshot).error() == ca::RebuildError::UnknownAsset);
  CHECK(host->status(ca::RebuildId{f.unknown}).error() == ca::RebuildError::UnknownJob);
  REQUIRE(fs::create_directory(f.staging / "assets/materials"));
  write(f.staging / "assets/materials/material.stamp", "already current");
  auto accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  CHECK(accepted.value().state == ca::RebuildState::Running);
  CHECK(host->start(f.guid, f.snapshot).error() == ca::RebuildError::Busy);
  const auto result = f.complete(*host, accepted.value().id);
  REQUIRE(result.generation);
  CHECK(result.state == ca::RebuildState::Succeeded);
  CHECK_FALSE(host->status({}).value().activeGeneration);
  CHECK(host->status({}).value().job == result);
  CHECK(read(f.seed / "artifact") == "old");
  CHECK(read(f.published / as::toString(result.generation->value) / "artifact") == "new");
  host.reset();
  auto selected = f.config();
  selected.generation = result.generation;
  auto activated = AssetRebuildHost::open(selected);
  REQUIRE(activated.isOk());
  CHECK(activated.value()->status({}).value().activeGeneration == result.generation);
  CHECK(activated.value()->catalogPath().parent_path().filename().string() == as::toString(result.generation->value));
  CHECK_FALSE(activated.value()->admissionError());
  CHECK(activated.value()->status(result.id).error() == ca::RebuildError::UnknownJob);
  const auto foreignRoot = f.base / "foreign";
  const auto relative = fs::path(as::toString(result.generation->value)) / "generation.json";
  REQUIRE(fs::create_directories((foreignRoot / relative).parent_path()));
  REQUIRE(fs::copy_file(f.published / relative, foreignRoot / relative));
  auto foreign = selected;
  foreign.profile = f.base / "foreign.json";
  Fixture::writeProfile(foreign.profile, f.staging, foreignRoot);
  const auto refused = AssetRebuildHost::open(foreign);
  REQUIRE(refused.isErr());
  CHECK(refused.error() == "Unknown, incomplete or foreign asset generation.");
  selected.generation = ca::GenerationId{f.unknown};
  CHECK(AssetRebuildHost::open(selected).isErr());
  const auto partial = f.published / as::toString(f.unknown);
  REQUIRE(fs::create_directory(partial));
  CHECK(AssetRebuildHost::open(selected).isErr());
}

TEST_CASE("asset rebuild: first material build in a configure-only workspace", "[runtime][asset-rebuild]") {
  Fixture f(false);
  REQUIRE(fs::is_regular_file(f.staging / "CMakeCache.txt"));
  REQUIRE_FALSE(fs::exists(f.staging / "assets/materials"));
  REQUIRE_FALSE(fs::exists(f.staging / "asset_catalog.txt"));
  REQUIRE_FALSE(fs::exists(f.staging / "artifact"));
  auto host = f.host();
  const auto accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  const auto result = f.complete(*host, accepted.value().id);
  INFO((result.failure ? result.failure->diagnostic : "no failure"));
  REQUIRE(result.state == ca::RebuildState::Succeeded);
  REQUIRE(result.generation);
  CHECK(fs::is_regular_file(f.staging / "assets/materials/material.stamp"));
  CHECK_FALSE(fs::exists(f.marker));
  CHECK_FALSE(host->status({}).value().activeGeneration);
  CHECK(read(f.seed / "artifact") == "old");
  CHECK(read(f.published / as::toString(result.generation->value) / "artifact") == "new");
}

TEST_CASE("asset rebuild: ordinary failure and identity change preserve the seed and permit retry", "[runtime][asset-rebuild]") {
  Fixture f;
  auto host = f.host();
  f.mode("fail");
  auto accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  const auto failed = f.complete(*host, accepted.value().id);
  REQUIRE(failed.failure);
  CHECK(failed.failure->code == ca::RebuildFailureCode::BuildFailed);
  CHECK(failed.failure->exitCode.has_value());
  CHECK_FALSE(fs::exists(f.marker));
  f.mode("ok");
  f.catalog(f.staging, f.unknown);
  accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  const auto changed = f.complete(*host, accepted.value().id);
  REQUIRE(changed.failure);
  CHECK(changed.failure->code == ca::RebuildFailureCode::IdentityChanged);
  CHECK_FALSE(fs::exists(f.marker));
  CHECK(read(f.seed / "artifact") == "old");
  CHECK(host->status(failed.id).value().job == failed);
  f.catalog(f.staging, f.guid);
  accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  CHECK(f.complete(*host, accepted.value().id).state == ca::RebuildState::Succeeded);
}

TEST_CASE("asset rebuild: timeout and invalid staging require offline repair", "[runtime][asset-rebuild]") {
  Fixture f;
  auto config = f.config();
  SECTION("timeout terminates the contained process tree") {
    config.timeout = 400ms;
    f.mode("slow");
  }
  SECTION("missing output after assembly") {
    REQUIRE(fs::remove(f.staging / "metadata"));
  }
  auto opened = AssetRebuildHost::open(config);
  REQUIRE(opened.isOk());
  auto host = std::move(opened.value());
  const auto accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  const auto failed = f.complete(*host, accepted.value().id);
  REQUIRE(failed.failure);
  CHECK(failed.failure->code == (config.timeout == 400ms ? ca::RebuildFailureCode::TimedOut
                                                        : ca::RebuildFailureCode::InvalidOutput));
  CHECK(fs::exists(f.marker));
  CHECK(host->start(f.guid, f.snapshot).error() == ca::RebuildError::WorkspaceNeedsRepair);
  host.reset();
  host = f.host();
  CHECK(host->start(f.guid, f.snapshot).error() == ca::RebuildError::WorkspaceNeedsRepair);
  CHECK(read(f.seed / "artifact") == "old");
}

TEST_CASE("asset rebuild: launch refusals are bounded and retain the newest 16 job records",
          "[runtime][asset-rebuild]") {
  Fixture f;
  auto config = f.config();
  config.cmake = f.base / "missing-cmake.exe";
  auto opened = AssetRebuildHost::open(config);
  REQUIRE(opened.isOk());
  auto host = std::move(opened.value());
  std::vector<ca::RebuildId> ids;
  for (int i = 0; i != 17; ++i) {
    const auto accepted = host->start(f.guid, f.snapshot);
    REQUIRE(accepted.isOk());
    REQUIRE(std::find(ids.begin(), ids.end(), accepted.value().id) == ids.end());
    ids.push_back(accepted.value().id);
    const auto failed = f.complete(*host, ids.back());
    REQUIRE(failed.failure);
    CHECK(failed.failure->code == ca::RebuildFailureCode::LaunchFailed);
    CHECK_FALSE(fs::exists(f.marker));
  }
  CHECK(host->status(ids.front()).error() == ca::RebuildError::UnknownJob);
  CHECK(host->status(ids[1]).isOk());
  CHECK(host->status({}).value().job->id == ids.back());
}

TEST_CASE("asset rebuild: an escaping producer stamp cannot touch the seed", "[runtime][asset-rebuild]") {
  Fixture f;
  std::string_view stamp;
  SECTION("escape at the start of the path") { stamp = "../seed/artifact"; }
  SECTION("escape after a missing directory") { stamp = "missing/../../seed/artifact"; }
  const std::string inventory = "atlantis.asset-producers/1\tfixture\nmaterial\tassets\ta.material.txt\t" +
      as::toString(f.guid) + "\t" + std::string(stamp) + "\t-\n";
  for (const auto& path : {f.seed, f.staging}) write(path / "assets/producers-Debug.txt", inventory);
  auto host = f.host();
  const auto accepted = host->start(f.guid, f.snapshot);
  REQUIRE(accepted.isOk());
  const auto failed = f.complete(*host, accepted.value().id);
  REQUIRE(failed.failure);
  CHECK(failed.failure->code == ca::RebuildFailureCode::LaunchFailed);
  CHECK(read(f.seed / "artifact") == "old");
  CHECK(fs::is_regular_file(f.marker));
  CHECK(host->start(f.guid, f.snapshot).error() == ca::RebuildError::WorkspaceNeedsRepair);
}

TEST_CASE("asset import: resume rejects an invalid catalog delta before publication", "[runtime][asset-import]") {
  Fixture f;
  const auto project = f.base / "project";
  const auto registered = project / "registered/managed_sample";
  REQUIRE(fs::create_directories(registered));
  const auto canonicalProject = fs::canonical(project);
  const auto root = as::parseAssetGuid("00860086-0086-4086-8086-008600860086").value();
  const as::CatalogSourceEntry baseEntry{f.guid, as::CatalogAssetType::Material, as::CatalogRoot::Assets, "a.material.txt"};
  const as::CatalogSourceEntry importEntry{root, as::CatalogAssetType::GltfImport, as::CatalogRoot::Content,
                                           "managed_sample/source.gltf"};
  write(registered / "source.gltf", "fixture");
  write(registered / "catalog-source.txt", as::serializeAssetCatalogSource({importEntry}));
  write(f.profile, "{\"stagingBuild\":\"" + f.staging.generic_string() + "\",\"publicationRoot\":\"" +
      f.published.generic_string() + "\",\"importProject\":\"" + canonicalProject.generic_string() + "\"}");
  for (const auto& build : {f.seed, f.staging})
    write(build / "assets/managed-project.txt", canonicalProject.generic_string());
  write(f.staging / "assets/managed-pending.txt", {});
  write(f.seed / "assets/managed-catalog-source.txt", as::serializeAssetCatalogSource({baseEntry}));
  write(f.staging / "assets/managed-catalog-source.txt", as::serializeAssetCatalogSource({baseEntry, importEntry}));
  write(f.staging / "assets/producers-Debug.txt", read(f.seed / "assets/producers-Debug.txt") +
      "gltf_import\tcontent\tmanaged_sample/source.gltf\t" + as::toString(root) +
      "\tmanaged_sample/managed_sample.stamp\tmanaged_sample/import/managed_sample/managed_sample.scene.txt\n");
  const auto importedDir = f.staging / "managed_sample/import/managed_sample";
  REQUIRE(fs::create_directories(importedDir));
  write(importedDir / "managed_sample.scene.txt", "fixture");
  const auto old = as::parseAssetCatalogRecords(read(f.seed / "asset_catalog.txt"));
  REQUIRE(old.isOk());
  const auto derived = [&](as::CatalogAssetType type, std::string_view subKey) {
    const auto guid = as::deriveAssetGuid(root, subKey);
    return as::AssetCatalogRecord{guid, as::assetKey(guid), type,
        {as::CatalogRoot::Content, importEntry.path, std::string(subKey)},
        "artifact", "metadata", 1, {}, "fixture/1", {}};
  };
  const auto mesh = derived(as::CatalogAssetType::Mesh, "mesh/0/0");
  const auto scene = derived(as::CatalogAssetType::Scene, "scene");
  write(importedDir.parent_path() / "import.catalog.txt", as::serializeAssetCatalog({mesh, scene}));
  auto extra = old.value().front();
  extra.guid = f.unknown;
  extra.assetId = as::assetKey(extra.guid);
  extra.source.path = "unexpected.material.txt";
  auto changed = old.value().front();
  changed.source.path = "changed.material.txt";
  auto colliding = extra;
  colliding.guid = mesh.guid;
  colliding.assetId = mesh.assetId;
  auto expectedCode = ca::ImportFailureCode::IdentityConflict;
  SECTION("unrelated addition") {
    write(f.staging / "asset_catalog.txt", as::serializeAssetCatalog({old.value().front(), mesh, scene, extra}));
  }
  SECTION("missing imported record") {
    write(f.staging / "asset_catalog.txt", as::serializeAssetCatalog({old.value().front(), mesh}));
    expectedCode = ca::ImportFailureCode::InvalidOutput;
  }
  SECTION("changed existing identity") {
    write(f.staging / "asset_catalog.txt", as::serializeAssetCatalog({changed, mesh, scene}));
  }
  SECTION("duplicate imported GUID and key") {
    write(f.staging / "asset_catalog.txt", as::serializeAssetCatalog({old.value().front(), mesh, scene, colliding}));
    expectedCode = ca::ImportFailureCode::InvalidOutput;
  }
  write(f.base / "producer.cmake", "# The fixture supplies already assembled records.\n");

  auto host = f.host();
  CHECK(host->startImport("sample", ca::ImportMode::Register, f.snapshot).error() == ca::ImportError::NameConflict);
  const auto accepted = host->startImport("sample", ca::ImportMode::Resume, f.snapshot);
  REQUIRE(accepted.isOk());
  const auto limit = std::chrono::steady_clock::now() + 20s;
  auto result = host->importStatus(accepted.value().id);
  while (result.isOk() && result.value().job && result.value().job->state == ca::RebuildState::Running &&
         std::chrono::steady_clock::now() < limit) {
    std::this_thread::sleep_for(20ms);
    result = host->importStatus(accepted.value().id);
  }
  REQUIRE(result.isOk());
  REQUIRE(result.value().job);
  REQUIRE(result.value().job->failure);
  CHECK(result.value().job->failure->code == expectedCode);
  CHECK(result.value().job->failure->registeredRootGuid == root);
  CHECK_FALSE(result.value().job->generation);
  const bool repair = expectedCode == ca::ImportFailureCode::InvalidOutput;
  CHECK(fs::exists(project / ".atlantis-import-dirty") == repair);
  CHECK(fs::exists(f.marker) == repair);
}

TEST_CASE("material edit verifies the prepared generation before committing source", "[runtime][asset-material]") {
  Fixture f;
  namespace material = atlantis::runtime::material_authoring;
  const auto guid = [](const char* value) { return as::parseAssetGuid(value).value(); };
  const auto baseColor = guid("859a82af-a31e-457d-bc46-274c1a80ffba");
  const auto normal = guid("5e99757a-d524-4aaf-b665-0d308aa7ae99");
  const auto project = f.base / "project";
  const auto registered = project / "registered/material_sample";
  const fs::path frozenSource = "assets/managed-material-sources/material_sample/source.material.txt";
  const fs::path artifact = "material.amaterial", metadata = "material.amaterial.meta.txt";
  const std::string key = "material_sample/source.material.txt";
  const auto staging = f.staging;
  const auto materialGuid = f.guid;
  auto config = f.config();
  bool corrupt = true;
  SECTION("unchanged prepared files can commit and publish") { corrupt = false; }
  SECTION("changed frozen source bytes") {
    config.afterMaterialPreparation = [=](const fs::path& dir) {
      auto bytes = read(dir / frozenSource);
      const auto end = bytes.find('\n');
      if (end != std::string::npos) bytes.insert(end, 1, '\r');
      std::ofstream(dir / frozenSource, std::ios::binary) << bytes;
    };
  }
  SECTION("changed cooked recipe with the same dependency edges") {
    config.afterMaterialPreparation = [=](const fs::path& dir) {
      fs::copy_file(staging / "other.amaterial", dir / artifact, fs::copy_options::overwrite_existing);
      fs::copy_file(staging / "other.amaterial.meta.txt", dir / metadata, fs::copy_options::overwrite_existing);
    };
  }
  SECTION("missing normal map catalog edge") {
    config.afterMaterialPreparation = [=](const fs::path& dir) {
      const auto file = dir / "asset_catalog.txt";
      auto catalog = as::parseAssetCatalogRecords(read(file));
      if (catalog.isErr()) throw std::runtime_error("prepared fixture catalog is unreadable");
      for (auto& record : catalog.value())
        if (record.guid == materialGuid) std::erase(record.dependencies, normal);
      std::ofstream(file, std::ios::binary) << as::serializeAssetCatalog(std::move(catalog.value()));
    };
  }

  REQUIRE(fs::create_directories(registered));
  const auto canonicalProject = fs::canonical(project);
  write(f.profile, "{\"stagingBuild\":\"" + f.staging.generic_string() + "\",\"publicationRoot\":\"" +
      f.published.generic_string() + "\",\"importProject\":\"" + canonicalProject.generic_string() + "\"}");
  const as::CatalogSourceEntry entry{f.guid, as::CatalogAssetType::Material, as::CatalogRoot::Content, key};
  const auto fragment = as::serializeAssetCatalogSource({entry});
  write(registered / "catalog-source.txt", fragment);
  ca::MaterialRecipe original{{baseColor}};
  auto updated = original;
  updated.values.normalMap = normal;
  updated.values.emissiveTexture = baseColor;
  updated.values.emissiveFactor = {0.5f, 0.25f, 0.125f};
  const auto originalSource = as::serializeMaterialSource(material::source(original));
  write(registered / "source.material.txt", originalSource);
  write(f.staging / "assets/managed-pending.txt", {});
  write(f.base / "producer.cmake", "# This fixture supplies finished producer outputs.\n");
  const auto fixtureCatalog = as::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(fixtureCatalog.isOk());
  const auto inventory = "atlantis.asset-producers/1\tfixture\nmaterial\tcontent\t" + key + "\t" +
      as::toString(f.guid) + "\tassets/material_sample/source.stamp\t-\n";
  for (const auto& root : {f.seed, f.staging}) {
    write(root / "assets/producers-Debug.txt", inventory);
    write(root / "assets/managed-project.txt", canonicalProject.generic_string());
    write(root / "assets/managed-catalog-source.txt", fragment);
    REQUIRE(fs::create_directories((root / frozenSource).parent_path()));
    const auto& recipe = root == f.seed ? original : updated;
    write(root / frozenSource, as::serializeMaterialSource(material::source(recipe)));
    REQUIRE(as::cookMaterial((root / frozenSource).string(), key, f.guid, (root / artifact).string(),
                             (root / metadata).string()).isOk());
    auto target = as::parseAssetCatalogRecords(read(root / "asset_catalog.txt")).value().front();
    target.source = {as::CatalogRoot::Content, key, {}};
    target.artifact = artifact.generic_string();
    target.metadata = metadata.generic_string();
    target.artifactSchema = as::kMaterialArtifactSchemaVersion;
    target.dependencies = {baseColor};
    if (recipe.values.normalMap) target.dependencies.push_back(normal);
    std::vector records{std::move(target)};
    for (const auto& id : {baseColor, normal}) {
      const auto* texture = fixtureCatalog.value().find(id);
      REQUIRE(texture != nullptr);
      auto copy = *texture;
      copy.artifact = as::toString(id) + ".atex";
      copy.metadata = copy.artifact + ".meta.txt";
      REQUIRE(fs::copy_file(texture->artifact, root / copy.artifact));
      REQUIRE(fs::copy_file(texture->metadata, root / copy.metadata));
      records.push_back(std::move(copy));
    }
    write(root / "asset_catalog.txt", as::serializeAssetCatalog(std::move(records)));
  }
  auto other = updated;
  other.values.emissiveFactor[0] = 0.75f;
  write(f.staging / "other.material.txt", as::serializeMaterialSource(material::source(other)));
  REQUIRE(as::cookMaterial((f.staging / "other.material.txt").string(), key, f.guid,
      (f.staging / "other.amaterial").string(), (f.staging / "other.amaterial.meta.txt").string()).isOk());
  auto catalog = as::loadAssetCatalog(config.seedCatalog.string());
  REQUIRE(catalog.isOk());
  auto opened = AssetRebuildHost::open(config);
  INFO((opened.isErr() ? opened.error() : std::string{}));
  REQUIRE(opened.isOk());
  const auto accepted = opened.value()->saveMaterial(ca::EditMaterial{f.guid, {}, original.values, updated.values},
      std::make_shared<const as::AssetCatalog>(std::move(catalog.value())), ca::MaterialRecipeVersion::WithMaps);
  REQUIRE(accepted.isOk());
  const auto limit = std::chrono::steady_clock::now() + 20s;
  auto status = opened.value()->materialStatus(accepted.value().id);
  while (status.isOk() && status.value().job && status.value().job->state == ca::RebuildState::Running &&
         std::chrono::steady_clock::now() < limit) {
    std::this_thread::sleep_for(20ms);
    status = opened.value()->materialStatus(accepted.value().id);
  }
  REQUIRE(status.isOk());
  REQUIRE(status.value().job);
  const auto& job = *status.value().job;
  INFO((job.failure ? job.failure->diagnostic : "no failure"));
  CHECK_FALSE(status.value().workspaceNeedsRepair);
  if (corrupt) {
    REQUIRE(job.state == ca::RebuildState::Failed);
    REQUIRE(job.failure);
    CHECK(job.failure->stage == ca::MaterialStage::Validation);
    CHECK(job.failure->code == ca::MaterialFailureCode::InvalidOutput);
    CHECK(job.failure->sourceState == ca::RegistrationState::NotCommitted);
    CHECK_FALSE(job.generation);
    CHECK(read(registered / "source.material.txt") == originalSource);
    for (const auto& file : fs::directory_iterator(f.published)) CHECK_FALSE(file.is_directory());
  } else {
    REQUIRE(job.state == ca::RebuildState::Succeeded);
    REQUIRE(job.generation);
    CHECK(job.material == f.guid);
    CHECK(read(registered / "source.material.txt") == as::serializeMaterialSource(material::source(updated)));
    CHECK(read(f.published / as::toString(job.generation->value) / frozenSource) ==
          read(registered / "source.material.txt"));
  }
}
