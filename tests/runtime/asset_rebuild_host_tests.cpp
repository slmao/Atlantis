#include "../../src/runtime/asset_rebuild_host.h"
#include "../../src/runtime/material_authoring.h"

#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <memory>
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

// One RGBA8 texel for either linear texture role.
constexpr unsigned char kLinearPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
      0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x68, 0x68, 0xf8, 0xff,
      0x1f, 0x00, 0x06, 0x82, 0x02, 0xff, 0xb3, 0xbe, 0x51, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
      0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
const std::string_view kLinearPngBytes{reinterpret_cast<const char*>(kLinearPng), sizeof(kLinearPng)};

constexpr unsigned char kReplacementPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
    0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9c, 0x63, 0x10, 0xd9, 0x68, 0xff, 0x1f, 0x00, 0x03, 0xe5, 0x02, 0x04, 0x76, 0x96,
    0xf2, 0x73, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
const std::string_view kReplacementBytes{reinterpret_cast<const char*>(kReplacementPng), sizeof(kReplacementPng)};

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
    runCmake(L"-S " + quoted(base) + L" -B " + quoted(staging));
  }

  void runCmake(const std::wstring& args) const {
    auto cmd = quoted(cmake) + L" " + args;
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

TEST_CASE("material edit and Resume verify the prepared and published generation", "[runtime][asset-material]") {
  Fixture f;
  namespace material = atlantis::runtime::material_authoring;
  const auto guid = [](const char* value) { return as::parseAssetGuid(value).value(); };
  const auto baseColor = guid("859a82af-a31e-457d-bc46-274c1a80ffba");
  const auto normal = guid("5e99757a-d524-4aaf-b665-0d308aa7ae99");
  const auto variant = GENERATE(0, 1, 2);
  const bool useOcclusion = variant != 0;
  const bool combined = variant == 2;
  CAPTURE(variant);
  const std::string leaf = useOcclusion ? "occlusion.png" : "metallic-roughness.png";
  const auto mappedTexture = as::deriveAssetGuid(f.guid, leaf);
  const std::string mappedKey = "texture_sample/" + leaf;
  const fs::path mappedPng = fs::path("assets/managed-texture-sources") / mappedKey;
  const fs::path mappedArtifact = "assets/texture_sample.atex";
  const fs::path mappedMetadata = "assets/texture_sample.atex.meta.txt";
  const fs::path mappedEvidence = "assets/texture_sample.texture-evidence";
  const fs::path mappedStamp = "assets/texture_sample.stamp";
  const auto project = f.base / "project";
  const auto registered = project / "registered/material_sample";
  const fs::path frozenSource = "assets/managed-material-sources/material_sample/source.material.txt";
  const fs::path artifact = "material.amaterial", metadata = "material.amaterial.meta.txt";
  const std::string key = "material_sample/source.material.txt";
  const auto staging = f.staging;
  const auto materialGuid = f.guid;
  auto config = f.config();
  using Mode = ca::MaterialMode;
  using Stage = ca::MaterialStage;
  using Code = ca::MaterialFailureCode;
  const auto mode = GENERATE(Mode::Edit, Mode::Resume);
  CAPTURE(ca::toString(mode));
  const bool resume = mode == Mode::Resume;
  std::optional<Stage> failureStage = Stage::Validation;
  fs::path destination;
  const auto damagePng = [mappedPng](const fs::path& dir) {
    std::ofstream file(dir / mappedPng, std::ios::binary | std::ios::app);
    if (!(file << 'x')) throw std::runtime_error("Cannot damage the frozen PNG");
  };
  SECTION("unchanged prepared files can commit and publish") { failureStage.reset(); }
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
  SECTION("missing map catalog edge") {
    config.afterMaterialPreparation = [=](const fs::path& dir) {
      const auto file = dir / "asset_catalog.txt";
      auto catalog = as::parseAssetCatalogRecords(read(file));
      if (catalog.isErr()) throw std::runtime_error("prepared fixture catalog is unreadable");
      for (auto& record : catalog.value())
        if (record.guid == materialGuid && std::erase(record.dependencies, useOcclusion ? mappedTexture : normal) != 1)
          throw std::runtime_error("prepared material had no matching map edge");
      std::ofstream(file, std::ios::binary) << as::serializeAssetCatalog(std::move(catalog.value()));
    };
  }

  SECTION("changed frozen texture before commit") {
    config.afterMaterialPreparation = damagePng;
  }
  SECTION("uncertain publication with intact destination") {
    failureStage.reset();
    config.beforeMaterialPublication = [](const fs::path& from, const fs::path& to) { fs::rename(from, to); };
  }
  SECTION("uncertain publication with damaged destination") {
    failureStage = Stage::Publication;
    config.beforeMaterialPublication = [&](const fs::path& from, const fs::path& to) {
      fs::rename(from, to);
      destination = to;
      damagePng(to);
    };
  }

  REQUIRE(fs::create_directories(registered));
  const auto registeredTexture = project / "registered/texture_sample";
  REQUIRE(fs::create_directories(registeredTexture));
  const as::CatalogSourceEntry textureEntry{mappedTexture, as::CatalogAssetType::Texture, as::CatalogRoot::Content, mappedKey};
  write(registeredTexture / "catalog-source.txt", as::serializeAssetCatalogSource({textureEntry}));
  write(registeredTexture / leaf, kLinearPngBytes);
  const auto canonicalProject = fs::canonical(project);
  write(f.profile, "{\"stagingBuild\":\"" + f.staging.generic_string() + "\",\"publicationRoot\":\"" +
      f.published.generic_string() + "\",\"importProject\":\"" + canonicalProject.generic_string() + "\"}");
  const as::CatalogSourceEntry entry{f.guid, as::CatalogAssetType::Material, as::CatalogRoot::Content, key};
  const auto fragment = as::serializeAssetCatalogSource({entry});
  write(registered / "catalog-source.txt", fragment);
  ca::MaterialRecipe original{{baseColor}};
  auto updated = original;
  if (useOcclusion) {
    updated.values.occlusionMap = mappedTexture;
    if (combined) updated.values.normalMap = normal;
  } else {
    updated.values.normalMap = normal;
    updated.values.metallicRoughnessMap = mappedTexture;
  }
  updated.values.emissiveTexture = baseColor;
  updated.values.emissiveFactor = {0.5f, 0.25f, 0.125f};
  const auto originalSource = as::serializeMaterialSource(material::source(original));
  const auto updatedSource = as::serializeMaterialSource(material::source(updated));
  const auto& committedSource = resume ? updatedSource : originalSource;
  write(registered / "source.material.txt", committedSource);
  write(f.staging / "assets/managed-pending.txt", {});
  write(f.base / "producer.cmake", "# This fixture supplies finished producer outputs.\n");
  const auto fixtureCatalog = as::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(fixtureCatalog.isOk());
  const auto inventory = "atlantis.asset-producers/1\tfixture\nmaterial\tcontent\t" + key + "\t" +
      as::toString(f.guid) + "\tassets/material_sample/source.stamp\t-\ntexture\tcontent\t" + mappedKey + "\t" +
      as::toString(mappedTexture) + "\t" + mappedStamp.generic_string() + "\t-\n";
  for (const auto& root : {f.seed, f.staging}) {
    write(root / "assets/producers-Debug.txt", inventory);
    write(root / "assets/managed-project.txt", canonicalProject.generic_string());
    write(root / "assets/managed-catalog-source.txt", as::serializeAssetCatalogSource({entry, textureEntry}));
    REQUIRE(fs::create_directories((root / mappedPng).parent_path()));
    write(root / mappedPng, kLinearPngBytes);
    constexpr std::uint8_t rgba[] = {128, 128, 255, 255};
    REQUIRE(as::cookTexture(rgba, 1, 1, 4, as::TextureColorSpace::Unorm, mappedKey, mappedTexture,
                            root / mappedArtifact, root / mappedMetadata).isOk());
    f.runCmake(L"-Dpng=" + quoted(root / mappedPng) + L" -Dartifact=" + quoted(root / mappedArtifact) +
               L" -Dmetadata=" + quoted(root / mappedMetadata) + L" -Devidence=" + quoted(root / mappedEvidence) +
               L" -Dstamp=" + quoted(root / mappedStamp) + L" -P " + quoted(ATLANTIS_REBUILD_TEST_TEXTURE_EVIDENCE));
    REQUIRE(fs::create_directories((root / frozenSource).parent_path()));
    const auto& recipe = root == f.seed ? original : updated;
    write(root / frozenSource, as::serializeMaterialSource(material::source(recipe)));
    REQUIRE(as::cookMaterial((root / frozenSource).string(), key, f.guid, (root / artifact).string(),
                             (root / metadata).string()).isOk());
    auto target = as::parseAssetCatalogRecords(read(root / "asset_catalog.txt")).value().front();
    target.source = {as::CatalogRoot::Content, key, {}};
    target.artifact = artifact.generic_string();
    target.metadata = metadata.generic_string();
    const bool combinedRecipe = combined && recipe.values.occlusionMap.has_value();
    if (combinedRecipe) target.sourceSchema = as::kCombinedMaterialSourceSchemaVersion;
    target.artifactSchema = combinedRecipe ? as::kCombinedMaterialArtifactSchemaVersion :
        recipe.values.occlusionMap ? as::kOcclusionMaterialArtifactSchemaVersion :
        recipe.values.metallicRoughnessMap ? as::kMappedMaterialArtifactSchemaVersion : as::kMaterialArtifactSchemaVersion;
    target.dependencies = {baseColor};
    if (recipe.values.normalMap) target.dependencies.push_back(normal);
    if (recipe.values.metallicRoughnessMap || recipe.values.occlusionMap) target.dependencies.push_back(mappedTexture);
    std::vector records{std::move(target),
        as::AssetCatalogRecord{mappedTexture, as::assetKey(mappedTexture), as::CatalogAssetType::Texture,
            {as::CatalogRoot::Content, mappedKey, {}}, mappedArtifact.generic_string(),
            mappedMetadata.generic_string(), as::kTextureArtifactSchemaVersion, {}, "fixture/1", {}}};
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
  write(registeredTexture / "source.sha256", read(f.seed / mappedEvidence).substr(0, 65));
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
  const ca::MaterialSave request = resume ? ca::MaterialSave{ca::ResumeMaterial{f.guid}} :
      ca::MaterialSave{ca::EditMaterial{f.guid, {}, original.values, updated.values}};
  const auto accepted = opened.value()->saveMaterial(request,
      std::make_shared<const as::AssetCatalog>(std::move(catalog.value())),
      combined ? ca::MaterialRecipeVersion::WithCombinedMaps :
      useOcclusion ? ca::MaterialRecipeVersion::WithOcclusion : ca::MaterialRecipeVersion::WithMetallicRoughness);
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
  CHECK_FALSE(status.value().activeGeneration);
  const bool publicationFailed = failureStage == Stage::Publication;
  CHECK(status.value().workspaceNeedsRepair == publicationFailed);
  if (failureStage) {
    REQUIRE(job.state == ca::RebuildState::Failed);
    REQUIRE(job.failure);
    CHECK(job.failure->stage == *failureStage);
    CHECK(job.failure->code == (publicationFailed ? Code::PublishFailed : Code::InvalidOutput));
    const bool committed = resume || publicationFailed;
    CHECK(job.failure->sourceState ==
          (committed ? ca::RegistrationState::Committed : ca::RegistrationState::NotCommitted));
    if (committed) CHECK(job.failure->material == f.guid);
    CHECK_FALSE(job.generation);
    CHECK(read(registered / "source.material.txt") == (publicationFailed ? updatedSource : committedSource));
    if (publicationFailed) {
      REQUIRE(fs::is_directory(destination));
      CHECK(read(destination / mappedPng) == std::string(kLinearPngBytes) + 'x');
      opened.value().reset();
      config.generation = ca::GenerationId{as::parseAssetGuid(destination.filename().string()).value()};
      const auto reopened = AssetRebuildHost::open(config);
      REQUIRE(reopened.isErr());
      CHECK(reopened.error() == "Managed generation texture evidence is missing or invalid.");
    } else {
      for (const auto& file : fs::directory_iterator(f.published)) CHECK_FALSE(file.is_directory());
    }
  } else {
    REQUIRE(job.state == ca::RebuildState::Succeeded);
    REQUIRE(job.generation);
    CHECK(job.material == f.guid);
    CHECK(read(registered / "source.material.txt") == updatedSource);
    CHECK(read(f.published / as::toString(job.generation->value) / frozenSource) == updatedSource);
    opened.value().reset();
    config.generation = job.generation;
    const auto reopened = AssetRebuildHost::open(config);
    INFO((reopened.isErr() ? reopened.error() : std::string{}));
    REQUIRE(reopened.isOk());
  }
}

TEST_CASE("managed texture import and replacement validate frozen inventory and original fragment at settlement", "[runtime][asset-texture]") {
  Fixture f;
  const auto project = f.base / "project";
  const auto registered = project / "registered";
  const auto inbox = project / "inbox/sample/source.png";
  const fs::path inventory = "assets/producers-Debug.txt";
  using TextureKind = AssetRebuildHost::TextureKind;
  const auto kind = GENERATE(TextureKind::Color, TextureKind::Normal, TextureKind::MetallicRoughness, TextureKind::Occlusion);
  const std::string leaf = kind == TextureKind::Color ? "source.png" : kind == TextureKind::Occlusion ? "occlusion.png" :
      kind == TextureKind::MetallicRoughness ? "metallic-roughness.png" : "normal.png";
  CAPTURE(leaf);
  REQUIRE(fs::create_directories(registered));
  REQUIRE(fs::create_directories(inbox.parent_path()));
  const auto canonicalProject = fs::canonical(project);
  write(inbox, kLinearPngBytes);
  const as::CatalogSourceEntry baseEntry{f.guid, as::CatalogAssetType::Material, as::CatalogRoot::Assets, "a.material.txt"};
  write(f.profile, "{\"stagingBuild\":\"" + f.staging.generic_string() + "\",\"publicationRoot\":\"" +
      f.published.generic_string() + "\",\"importProject\":\"" + canonicalProject.generic_string() + "\"}");
  for (const auto& build : {f.seed, f.staging}) {
    write(build / "assets/managed-project.txt", canonicalProject.generic_string());
    write(build / "assets/managed-catalog-source.txt", as::serializeAssetCatalogSource({baseEntry}));
  }
  write(f.staging / "assets/managed-pending.txt", {});
  write(f.staging / "cooker-path.txt", ATLANTIS_REBUILD_TEST_COOKER);
  write(f.staging / "evidence-path.txt", ATLANTIS_REBUILD_TEST_TEXTURE_EVIDENCE);
  write(f.staging / "leaf.txt", leaf);
  write(f.base / "producer.cmake", R"cmake(
file(READ "assets/managed-project.txt" project)
file(READ "assets/managed-pending.txt" pending)
file(READ "cooker-path.txt" cooker)
set(frozen "${CMAKE_CURRENT_BINARY_DIR}/assets/managed-texture-sources")
set(asset "${CMAKE_CURRENT_BINARY_DIR}/assets/texture_sample")
file(READ "leaf.txt" leaf)
set(key "texture_sample/${leaf}")
set(color unorm)
if(leaf STREQUAL "source.png")
  set(color srgb)
endif()
file(MAKE_DIRECTORY "${frozen}/texture_sample")
file(COPY_FILE "${project}/${pending}/${leaf}" "${frozen}/${key}")
file(STRINGS "${project}/${pending}/catalog-source.txt" fragment)
list(GET fragment 2 texture)
string(REGEX MATCH "guid=([0-9a-f-]+)" matched "${texture}")
set(guid "${CMAKE_MATCH_1}")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/seed/assets/managed-catalog-source.txt" base REGEX "^asset:")
list(APPEND base "${texture}")
list(LENGTH base entry_count)
list(JOIN base "\n" source_entries)
file(WRITE "assets/managed-catalog-source.txt"
     "atlantis_asset_catalog_source_version: 1\nentry_count: ${entry_count}\n${source_entries}\n")
file(READ "${CMAKE_CURRENT_LIST_DIR}/seed/assets/producers-Debug.txt" inventory)
execute_process(COMMAND "${cooker}" --kind=texture "--source=${frozen}/${key}" "--asset-root=${frozen}"
  "--output-dir=${CMAKE_CURRENT_BINARY_DIR}/assets" "--stamp=${asset}.stamp" "--color-space=${color}"
  "--catalog-source=${CMAKE_CURRENT_BINARY_DIR}/assets/managed-catalog-source.txt" "--catalog-id=content:${key}"
  COMMAND_ERROR_IS_FATAL ANY)
file(STRINGS "${asset}.atex.catalog.txt" texture_catalog REGEX "^record:")
string(REPLACE "${CMAKE_CURRENT_BINARY_DIR}/assets/" "assets/" texture_catalog "${texture_catalog}")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/seed/asset_catalog.txt" material_catalog REGEX "^record:")
set(records "${texture_catalog}" "${material_catalog}")
list(SORT records)
list(JOIN records "\n" records)
file(CONFIGURE OUTPUT "asset_catalog.txt" CONTENT "atlantis_asset_catalog_version: 1\nrecord_count: 2\n${records}\n"
  @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT "assets/producers-Debug.txt" CONTENT
  "${inventory}texture\tcontent\t${key}\t${guid}\tassets/texture_sample.stamp\t-\n" @ONLY NEWLINE_STYLE UNIX)
file(READ "evidence-path.txt" script)
execute_process(COMMAND "${CMAKE_COMMAND}" "-Dpng=${frozen}/${key}" "-Dartifact=${asset}.atex"
  "-Dmetadata=${asset}.atex.meta.txt" "-Devidence=${asset}.texture-evidence" "-Dstamp=${asset}.stamp"
  -P "${script}" COMMAND_ERROR_IS_FATAL ANY)
)cmake");

  auto config = f.config();
  const auto changeHeader = [inventory](const fs::path& root) {
    const auto path = root / inventory;
    auto bytes = read(path);
    bytes.replace(bytes.find("\tfixture"), sizeof("\tfixture") - 1, "\tdifferent-checkout");
    if (!(std::ofstream(path, std::ios::binary) << bytes)) throw std::runtime_error("Cannot change test header");
  };
  auto stage = ca::ImportStage::Validation;
  auto code = ca::ImportFailureCode::InvalidOutput;
  bool moved = false;
  bool succeeds = false;
  enum class Replacement { None, InvalidRetry, Frozen, Tamper, Exchange, PublishRefused, Unknown, Confirmed };
  Replacement reimport = Replacement::None;
  enum class Rename {
    None, Success, CorruptPrepared, Rollback, Unknown, Confirmed,
    ActiveHeader, PreparedHeader, FinalHeader, StagedNext, LinkedNext, StagedPrevious, LateTimeout
  };
  Rename rename = Rename::None;
  enum class Removal {
    None, Success, SourceOnly, Incomplete, Stale, LateStale, Rollback, Unknown, ExtraFile, LinkedInbox,
    OverlayChanged, OverlayChangedLate, CorruptOldNames, LockedSelection
  };
  Removal removal = Removal::None;
  SECTION("registered color texture display rename") {
    if (kind != TextureKind::Color) return;
    succeeds = true;
    SECTION("verified generation, stale writers, and return to the fallback") { rename = Rename::Success; }
    SECTION("different valid label in prepared folder is refused") { rename = Rename::CorruptPrepared; }
    SECTION("definitely unpublished after exchange rolls back") { rename = Rename::Rollback; }
    SECTION("different valid label in final folder keeps repair evidence") { rename = Rename::Unknown; }
    SECTION("exact final folder confirms publication") { rename = Rename::Confirmed; }
    SECTION("an altered active inventory header is rejected after activation") { rename = Rename::ActiveHeader; }
    SECTION("matching altered active and prepared headers are rejected") { rename = Rename::PreparedHeader; }
    SECTION("matching altered active and final headers do not confirm publication") { rename = Rename::FinalHeader; }
    SECTION("different staged name bytes cannot replace the committed map") { rename = Rename::StagedNext; }
    SECTION("identical staged name bytes with multiple links are refused") { rename = Rename::LinkedNext; }
    SECTION("a damaged staged backup cannot overwrite the committed map") { rename = Rename::StagedPrevious; }
    SECTION("timeout during final pre-exchange validation prevents commit") { rename = Rename::LateTimeout; }
  }
  SECTION("registered color texture replacement") {
    if (kind != TextureKind::Color) return;
    succeeds = true;
    SECTION("invalid input preserves the old registration and allows a corrected retry") { reimport = Replacement::InvalidRetry; }
    SECTION("inbox edits after the snapshot do not affect the intended publication") { reimport = Replacement::Frozen; }
    SECTION("frozen evidence tamper fails before the unit is exchanged") { reimport = Replacement::Tamper; }
    SECTION("failed source placement restores the backup before permitting retry") { reimport = Replacement::Exchange; }
    SECTION("definitely unpublished with a blocked rename restores the old unit") { reimport = Replacement::PublishRefused; }
    SECTION("a damaged final destination remains unknown and holds the workspace") { reimport = Replacement::Unknown; }
    SECTION("an intact destination proves publication even after an uncertain move") { reimport = Replacement::Confirmed; }
  }
  SECTION("registered color texture removal") {
    if (kind != TextureKind::Color) return;
    succeeds = true;
    SECTION("successful removal retains PNG and permits activation") { removal = Removal::Success; }
    SECTION("current recipe blocks even with no cooked edge") { removal = Removal::SourceOnly; }
    SECTION("an unverified material recipe cannot produce Ready") { removal = Removal::Incomplete; }
    SECTION("confirmed input bytes changed with a fixed timestamp") { removal = Removal::Stale; }
    SECTION("input changes after candidate preparation") { removal = Removal::LateStale; }
    SECTION("definite nonpublication restores the sealed unit") { removal = Removal::Rollback; }
    SECTION("an altered final directory holds writes") { removal = Removal::Unknown; }
    SECTION("unexpected registered content is preserved") { removal = Removal::ExtraFile; }
    SECTION("an aliased inbox is refused") { removal = Removal::LinkedInbox; }
    SECTION("an overlay edit with the original timestamp invalidates confirmation") { removal = Removal::OverlayChanged; }
    SECTION("an overlay edit with the original timestamp before commit invalidates confirmation") {
      removal = Removal::OverlayChangedLate;
    }
    SECTION("a damaged old name table prevents either rollback write") { removal = Removal::CorruptOldNames; }
    SECTION("a locked temporary file leaves the published job and its recovery record") { removal = Removal::LockedSelection; }
  }
  const auto checkout = fs::canonical(f.base) / "checkout";
  const auto overlay = checkout / "assets/bistro/bistro_overlay.scene.txt";
  const bool bistroProof = removal == Removal::OverlayChanged || removal == Removal::OverlayChangedLate;
  if (removal != Removal::None) {
    const auto bistro = fs::canonical(f.base) / "bistro";
    const std::string header = "atlantis.asset-producers/1\t" + checkout.generic_string() + "\tfixture\tx64\tDebug" +
        (bistroProof ? "\t" + bistro.generic_string() : "");
    auto declarations = as::serializeAssetCatalogSource({baseEntry});
    if (bistroProof) {
      fs::create_directories(bistro);
      write(bistro / "bistro.gltf", "fixture import input");
      fs::create_directories(overlay.parent_path());
      write(overlay, "fixture overlay");
      declarations = as::serializeAssetCatalogSource({baseEntry,
          {f.unknown, as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, "bistro/bistro.gltf"}});
    }
    for (const auto& build : {f.seed, f.staging}) {
      auto bytes = read(build / inventory);
      bytes.replace(0, bytes.find('\n'), header);
      if (bistroProof) {
        bytes += "gltf_import\tcontent\tbistro/bistro.gltf\t" + as::toString(f.unknown) +
            "\tbistro/bistro.stamp\tbistro/generated.scene.txt\n";
        fs::create_directories(build / "bistro");
        write(build / "bistro/generated.scene.txt", "fixture generated scene");
        write(build / "assets/managed-catalog-source.txt", declarations);
      }
      write(build / inventory, bytes);
    }
    for (const fs::path relative : {"CMakeLists.txt", "assets/CMakeLists.txt", "src/asset_system/CMakeLists.txt",
                                  "cmake/AtlantisManagedImports.cmake", "src/tools/gltf_importer/CMakeLists.txt"}) {
      fs::create_directories((checkout / relative).parent_path());
      write(checkout / relative, "# configured fixture\n");
    }
    write(checkout / "assets/asset_catalog.txt", declarations);
  }
  SECTION("unchanged inputs publish and activate") { succeeds = true; }
  SECTION("changed prepared header refuses both renames") {
    config.afterTexturePreparation = [&](const fs::path& dir, const fs::path&) { changeHeader(dir); };
  }
  SECTION("changed original fragment refuses both renames even when the entry still parses") {
    stage = ca::ImportStage::Registration;
    code = ca::ImportFailureCode::RegisterFailed;
    config.afterTexturePreparation = [](const fs::path&, const fs::path& fragment) {
      auto bytes = read(fragment);
      bytes.replace(bytes.find("entry_count: 1"), sizeof("entry_count: 1") - 1, "entry_count: 01");
      if (!(std::ofstream(fragment, std::ios::binary) << bytes)) throw std::runtime_error("Cannot change test fragment");
    };
  }
  SECTION("ambiguous rename with an intact destination confirms publication") {
    config.beforeTexturePublication = [&](const fs::path& from, const fs::path& to) { fs::rename(from, to); };
    succeeds = true;
  }
  SECTION("ambiguous rename with a changed destination header refuses confirmation") {
    config.beforeTexturePublication = [&](const fs::path& from, const fs::path& to) {
      fs::rename(from, to);
      changeHeader(to);
    };
    stage = ca::ImportStage::Publication;
    code = ca::ImportFailureCode::PublishFailed;
    moved = true;
  }
  auto opened = AssetRebuildHost::open(config);
  INFO((opened.isErr() ? opened.error() : std::string{}));
  REQUIRE(opened.isOk());
  const auto admission = opened.value()->startTextureImport("sample", ca::ImportMode::Register, f.snapshot, kind);
  REQUIRE(admission.isOk());
  const auto limit = std::chrono::steady_clock::now() + 20s;
  auto status = opened.value()->textureImportStatus(admission.value().id, kind);
  while (status.isOk() && status.value().job && status.value().job->state == ca::RebuildState::Running &&
         std::chrono::steady_clock::now() < limit) {
    std::this_thread::sleep_for(20ms);
    status = opened.value()->textureImportStatus(admission.value().id, kind);
  }
  REQUIRE(status.isOk());
  REQUIRE(status.value().job);
  const auto& job = *status.value().job;
  INFO((job.failure ? job.failure->diagnostic : "no failure"));
  CHECK(read(inbox) == kLinearPngBytes);
  CHECK(fs::exists(registered / "texture_sample") == (moved || succeeds));
  if (succeeds) {
    REQUIRE(job.state == ca::RebuildState::Succeeded);
    REQUIRE(job.generation);
    CHECK_FALSE(status.value().workspaceNeedsRepair);
    CHECK(read(registered / "texture_sample" / leaf) == kLinearPngBytes);
    const auto wrong = kind == TextureKind::Normal ? TextureKind::Occlusion : TextureKind::Normal;
    CHECK(opened.value()->textureImportStatus(admission.value().id, wrong).error() == ca::ImportError::UnknownJob);
    const auto previousGeneration = *job.generation;
    REQUIRE(job.rootGuid);
    const auto target = *job.rootGuid;
    const auto oldRoot = f.published / as::toString(previousGeneration.value);
    const auto unit = registered / "texture_sample";
    const auto oldFragment = read(unit / "catalog-source.txt");
    opened.value().reset();
    config.generation = previousGeneration;
    const auto substituteName = [](const fs::path& root) {
      const auto file = root / "generation.json";
      auto data = read(file);
      const auto position = data.find("display_alias");
      if (position == std::string::npos) throw std::runtime_error("Missing fixture label");
      data.replace(position, std::string_view("display_alias").size(), "different_alias");
      if (!(std::ofstream(file, std::ios::binary) << data)) throw std::runtime_error("Cannot replace label");
    };
    std::atomic<bool> waiting = false, proceed = false;
    if (rename == Rename::Success) config.afterRenamePreparation = [&](const fs::path&) {
      waiting = true;
      const auto limit = std::chrono::steady_clock::now() + 5s;
      while (!proceed && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(10ms);
    };
    if (rename == Rename::CorruptPrepared) config.afterRenamePreparation = substituteName;
    if (rename == Rename::PreparedHeader) config.afterRenamePreparation = [=](const fs::path& prepared) {
      changeHeader(oldRoot);
      changeHeader(prepared);
    };
    if (rename == Rename::Rollback) config.beforeRenamePublication = [=](const fs::path& from, const fs::path&) {
      substituteName(from);
    };
    if (rename == Rename::Unknown || rename == Rename::Confirmed || rename == Rename::FinalHeader)
      config.beforeRenamePublication = [=](const fs::path& from, const fs::path& to) {
        fs::rename(from, to);
        if (rename == Rename::Unknown) substituteName(to);
        if (rename == Rename::FinalHeader) {
          changeHeader(oldRoot);
          changeHeader(to);
        }
      };
    if (rename == Rename::StagedNext) config.beforeRenameExchange = [](const fs::path& pending, auto) {
      if (!(std::ofstream(pending / "next", std::ios::binary) << "altered staged bytes"))
        throw std::runtime_error("Cannot change staged name bytes");
    };
    const auto linkedName = project / "linked-test-name";
    if (rename == Rename::LinkedNext) config.beforeRenameExchange = [=](const fs::path& pending, auto) {
      fs::create_hard_link(pending / "next", linkedName);
    };
    bool reachedDeadline = false;
    if (rename == Rename::LateTimeout) {
      config.timeout = 3s;
      config.beforeRenameExchange = [&](const fs::path&, auto deadline) {
        std::this_thread::sleep_until(deadline);
        reachedDeadline = true;
      };
    }
    if (reimport == Replacement::Frozen) config.afterTexturePreparation = [&](const fs::path&, const fs::path&) {
      std::ofstream(inbox, std::ios::binary) << "a newer inbox edit";
    };
    if (reimport == Replacement::Tamper) config.afterTexturePreparation = [&](const fs::path& root, const fs::path&) {
      changeHeader(root);
    };
    if (reimport == Replacement::Exchange) config.afterTextureBackup = [](const fs::path& candidate, const fs::path&) {
      fs::rename(candidate, candidate.parent_path() / "held-fixture");
    };
    HANDLE blockedPublish = INVALID_HANDLE_VALUE;
    if (reimport == Replacement::PublishRefused)
      config.beforeTexturePublication = [&](const fs::path& from, const fs::path&) {
        blockedPublish = CreateFileW(from.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (blockedPublish == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot hold the prepared directory.");
      };
    if (reimport == Replacement::Confirmed || reimport == Replacement::Unknown)
      config.beforeTexturePublication = [&](const fs::path& from, const fs::path& to) {
        fs::rename(from, to);
        if (reimport == Replacement::Unknown) changeHeader(to);
      };
    const auto authored = checkout / "assets/a.material.txt";
    const auto editOverlay = [overlay] {
      const auto timestamp = fs::last_write_time(overlay);
      if (!(std::ofstream(overlay, std::ios::binary) << "altered overlay"))
        throw std::runtime_error("Could not edit the fixture overlay.");
      fs::last_write_time(overlay, timestamp);
    };
    fs::path removalPending;
    std::string recoveryBytes;
    std::unique_ptr<void, decltype(&CloseHandle)> heldSelection(nullptr, &CloseHandle);
    if (removal != Removal::None) {
      const auto recipe = [&](as::AssetGuid referenced) {
        return "atlantis_material_source_version: 10\nkind: pbr_direct_lit\ntexture: " +
            as::toString(referenced) + "\nfilter: linear\naddress_mode: repeat\nbase_color_factor: 1 1 1 1\n"
            "metallic_factor: 0\nroughness_factor: 0.6\n";
      };
      write(authored, removal == Removal::Incomplete ? "invalid recipe" :
          recipe(removal == Removal::SourceOnly ? target : f.unknown));
      write(f.staging / "assets/texture-removal-profile.txt", "ordinary\n");
      fs::copy_file(f.staging / inventory, f.base / "registered-inventory.txt");
      fs::copy_file(f.staging / "assets/managed-catalog-source.txt", f.base / "registered-catalog.txt");
      write(f.base / "producer.cmake", R"cmake(
file(READ "assets/managed-pending.txt" pending)
if(pending MATCHES "^remove:")
  configure_file("${CMAKE_CURRENT_LIST_DIR}/seed/asset_catalog.txt" "${CMAKE_CURRENT_BINARY_DIR}/asset_catalog.txt" COPYONLY)
endif()
)cmake");
      write(f.base / "CMakeLists.txt", read(f.base / "CMakeLists.txt") + R"cmake(
file(READ "${CMAKE_BINARY_DIR}/assets/managed-pending.txt" pending)
if(pending MATCHES "^remove:" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/project/registered/texture_sample")
  set(evidence "${CMAKE_SOURCE_DIR}/seed/assets")
  set(roster "${evidence}/producers-Debug.txt")
  set(catalog "${evidence}/managed-catalog-source.txt")
else()
  set(roster "${CMAKE_SOURCE_DIR}/registered-inventory.txt")
  set(catalog "${CMAKE_SOURCE_DIR}/registered-catalog.txt")
endif()
configure_file("${roster}" "${CMAKE_BINARY_DIR}/assets/producers-Debug.txt" COPYONLY)
configure_file("${catalog}" "${CMAKE_BINARY_DIR}/assets/managed-catalog-source.txt" COPYONLY)
)cmake");
      if (removal == Removal::LateStale) config.afterRemovalPreparation = [=](const fs::path&) {
        if (!(std::ofstream(authored, std::ios::binary | std::ios::app) << "\n"))
          throw std::runtime_error("Could not edit the fixture recipe.");
      };
      if (removal == Removal::OverlayChangedLate)
        config.afterRemovalPreparation = [editOverlay](const fs::path&) { editOverlay(); };
      if (removal == Removal::Rollback) config.beforeRemovalPublication = [=](const fs::path& from, const fs::path&) {
        changeHeader(from);
      };
      if (removal == Removal::Unknown) config.beforeRemovalPublication = [=](const fs::path& from, const fs::path& to) {
        fs::rename(from, to);
        changeHeader(to);
      };
      if (removal == Removal::CorruptOldNames || removal == Removal::LockedSelection)
        config.beforeRemovalPublication = [&](const fs::path& from, const fs::path&) {
          for (const auto& attempt : fs::directory_iterator(project / ".pending")) {
            if (fs::exists(attempt.path() / "removal.json")) {
              removalPending = attempt.path();
              break;
            }
          }
          if (removalPending.empty()) throw std::runtime_error("Could not find the removal recovery record.");
          recoveryBytes = read(removalPending / "removal.json");
          if (removal == Removal::CorruptOldNames) {
            if (!(std::ofstream(removalPending / "old-names", std::ios::binary) << "altered backup"))
              throw std::runtime_error("Could not corrupt the staged name table.");
            changeHeader(from);
          } else {
            const auto locked = CreateFileW((removalPending / "selection").c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (locked == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not lock the staging selection.");
            heldSelection.reset(locked);
          }
        };
      if (removal == Removal::ExtraFile) write(unit / "keep.txt", "unexpected");
    }
    auto activated = AssetRebuildHost::open(config);
    INFO((activated.isErr() ? activated.error() : std::string{}));
    REQUIRE(activated.isOk());
    auto loaded = as::loadAssetCatalog(activated.value()->catalogPath().generic_string());
    REQUIRE(loaded.isOk());
    auto snapshot = std::make_shared<const as::AssetCatalog>(std::move(loaded.value()));
    if (kind != TextureKind::Color) {
      const auto refusal = activated.value()->start(target, snapshot);
      REQUIRE(refusal.isErr());
      CHECK(refusal.error() == ca::RebuildError::UnsupportedProducer);
      CHECK(activated.value()->renameTexture({target, previousGeneration, "display_alias"}, snapshot).error() ==
            ca::RenameTextureError::UnsupportedProducer);
    }
    if (removal != Removal::None) {
      using State = ca::TextureRemovalState;
      CAPTURE(static_cast<int>(removal));
      if (removal == Removal::CorruptOldNames) {
        const auto labelJob = activated.value()->renameTexture({target, previousGeneration, "display_alias"}, snapshot);
        REQUIRE(labelJob.isOk());
        const auto named = f.complete(*activated.value(), labelJob.value().id);
        REQUIRE(named.state == ca::RebuildState::Succeeded);
        REQUIRE(named.generation);
        activated.value().reset();
        config.generation = named.generation;
        activated = AssetRebuildHost::open(config);
        REQUIRE(activated.isOk());
        CHECK(activated.value()->displayName(target) == "display_alias");
        auto namedCatalog = as::loadAssetCatalog(activated.value()->catalogPath().generic_string());
        REQUIRE(namedCatalog.isOk());
        snapshot = std::make_shared<const as::AssetCatalog>(std::move(namedCatalog.value()));
      }
      if (removal == Removal::LinkedInbox) {
        REQUIRE(fs::remove(inbox));
        fs::create_hard_link(unit / "source.png", inbox);
      }
      const ca::TextureRemovalTarget requested{target, *config.generation};
      const auto admitted = activated.value()->checkTextureRemoval(requested, snapshot);
      INFO((admitted.isErr() ? ca::toString(admitted.error()) : std::string_view{}));
      REQUIRE(admitted.isOk());
      const auto checkLimit = std::chrono::steady_clock::now() + 20s;
      auto checked = activated.value()->textureRemovalCheckStatus(admitted.value().id);
      while (checked.isOk() && checked.value().state == State::Checking &&
             std::chrono::steady_clock::now() < checkLimit) {
        std::this_thread::sleep_for(20ms);
        checked = activated.value()->textureRemovalCheckStatus(admitted.value().id);
      }
      REQUIRE(checked.isOk());
      const auto& receipt = checked.value();
      INFO(receipt.diagnostic);
      CHECK(receipt.target == requested);
      if (removal == Removal::SourceOnly || removal == Removal::Incomplete ||
          removal == Removal::ExtraFile || removal == Removal::LinkedInbox) {
        CHECK(receipt.state == (removal == Removal::SourceOnly ? State::Referenced : State::CheckIncomplete));
        if (removal == Removal::SourceOnly) {
          REQUIRE(receipt.blockers.size() == 1);
          CHECK(receipt.blockers.front().asset == f.guid);
          CHECK(receipt.blockers.front().slot == ca::TextureReferenceSlot::BaseColor);
        }
        CHECK(activated.value()->removeTexture(requested, receipt.id).error() == ca::TextureRemovalError::StaleCheck);
        CHECK(fs::exists(unit));
        CHECK(read(inbox) == kLinearPngBytes);
        return;
      }
      REQUIRE(receipt.state == State::Ready);
      const auto publishedCount = [&] {
        return std::ranges::count_if(fs::directory_iterator(f.published), [](const auto& entry) {
          return as::parseAssetGuid(entry.path().filename().string()).isOk();
        });
      };
      const auto publishedBefore = publishedCount();
      if (removal == Removal::OverlayChanged) {
        const auto timestamp = fs::last_write_time(overlay);
        editOverlay();
        CHECK(fs::last_write_time(overlay) == timestamp);
      }
      if (removal == Removal::Stale) {
        const auto timestamp = fs::last_write_time(authored);
        auto bytes = read(authored);
        bytes.replace(bytes.find("0.6"), 3, "0.7");
        write(authored, bytes);
        fs::last_write_time(authored, timestamp);
      }
      const auto request = activated.value()->removeTexture(requested, receipt.id);
      REQUIRE(request.isOk());
      CHECK(activated.value()->removeTexture(requested, receipt.id).error() == ca::TextureRemovalError::StaleCheck);
      const auto settled = f.complete(*activated.value(), request.value().id);
      INFO((settled.failure ? settled.failure->diagnostic : "remove succeeded"));
      CHECK(read(inbox) == kLinearPngBytes);
      CHECK(fs::exists(oldRoot));
      if (removal == Removal::CorruptOldNames || removal == Removal::LockedSelection) {
        REQUIRE_FALSE(removalPending.empty());
        REQUIRE_FALSE(recoveryBytes.empty());
        CHECK(read(removalPending / "removal.json") == recoveryBytes);
        CHECK_FALSE(fs::exists(unit));
        CHECK(fs::exists(f.marker));
        CHECK(fs::exists(project / ".atlantis-import-dirty"));
        CHECK(activated.value()->checkTextureRemoval(requested, snapshot).error() == ca::TextureRemovalError::WorkspaceNeedsRepair);
        if (removal == Removal::CorruptOldNames) {
          CHECK(settled.state == ca::RebuildState::Failed);
          CHECK_FALSE(settled.generation);
          CHECK(publishedCount() == publishedBefore);
          CHECK(read(removalPending / "previous/source.png") == kLinearPngBytes);
          CHECK(read(removalPending / "old-names") == "altered backup");
          CHECK_FALSE(fs::exists(project / "texture-display-names.json"));
        } else {
          CHECK(heldSelection);
          REQUIRE(settled.state == ca::RebuildState::Succeeded);
          REQUIRE(settled.generation);
          heldSelection.reset();
          activated.value().reset();
          config.generation = settled.generation;
          auto reopened = AssetRebuildHost::open(config);
          REQUIRE(reopened.isOk());
          CHECK(reopened.value()->admissionError() == ca::RebuildError::WorkspaceNeedsRepair);
          reopened.value().reset();
          // A restart between clearing the markers and retiring the record
          // must also retain the hold.
          REQUIRE(fs::remove(f.marker));
          REQUIRE(fs::remove(project / ".atlantis-import-dirty"));
          reopened = AssetRebuildHost::open(config);
          REQUIRE(reopened.isOk());
          CHECK(reopened.value()->admissionError() == ca::RebuildError::WorkspaceNeedsRepair);
        }
        return;
      }
      if (removal != Removal::Success) {
        REQUIRE(settled.state == ca::RebuildState::Failed);
        CHECK_FALSE(settled.generation);
        CHECK(fs::exists(unit) == (removal != Removal::Unknown));
        CHECK(fs::exists(f.marker) == (removal == Removal::Unknown));
        if (removal == Removal::Stale || removal == Removal::LateStale || bistroProof)
          CHECK(settled.failure->code == ca::RebuildFailureCode::IdentityChanged);
        if (bistroProof) {
          CHECK(publishedCount() == publishedBefore);
          CHECK(read(unit / "source.png") == kLinearPngBytes);
        }
        return;
      }
      REQUIRE(settled.state == ca::RebuildState::Succeeded);
      REQUIRE(settled.generation);
      CHECK_FALSE(fs::exists(unit));
      CHECK_FALSE(fs::exists(f.marker));
      activated.value().reset();
      config.generation = settled.generation;
      auto reopened = AssetRebuildHost::open(config);
      REQUIRE(reopened.isOk());
      const auto current = as::loadAssetCatalog(reopened.value()->catalogPath().generic_string());
      REQUIRE(current.isOk());
      CHECK(current.value().find(target) == nullptr);
      return;
    }
    if (rename != Rename::None) {
      const auto names = project / "texture-display-names.json";
      const auto originalInventory = read(oldRoot / inventory);
      const auto publishedCount = [&] {
        return std::distance(fs::directory_iterator(f.published), fs::directory_iterator{});
      };
      const auto priorCount = publishedCount();
      CHECK_FALSE(activated.value()->displayName(target));
      CHECK(activated.value()->renameTexture({target, {f.unknown}, "display_alias"}, snapshot).error() ==
            ca::RenameTextureError::StaleSnapshot);
      CHECK(activated.value()->renameTexture({target, previousGeneration, "sample"}, snapshot).error() ==
            ca::RenameTextureError::Unchanged);
      CHECK(activated.value()->renameTexture({target, previousGeneration, "Display Alias"}, snapshot).error() ==
            ca::RenameTextureError::InvalidName);
      CHECK_FALSE(fs::exists(names));
      write(inbox, "rename must not use the inbox");
      write(f.staging / "artifact", "rename must not use staging");
      if (rename == Rename::ActiveHeader) changeHeader(oldRoot);
      const auto admitted = activated.value()->renameTexture({target, previousGeneration, "display_alias"}, snapshot);
      REQUIRE(admitted.isOk());
      if (rename == Rename::Success) {
        const auto preparationLimit = std::chrono::steady_clock::now() + 5s;
        while (!waiting && std::chrono::steady_clock::now() < preparationLimit) std::this_thread::sleep_for(10ms);
        CHECK(waiting);
        CHECK(activated.value()->renameTexture({target, previousGeneration, "also_waiting"}, snapshot).error() ==
              ca::RenameTextureError::Busy);
        proceed = true;
      }
      const auto renamed = f.complete(*activated.value(), admitted.value().id);
      INFO((renamed.failure ? renamed.failure->diagnostic : "no rename failure"));
      CHECK(renamed.asset == target);
      CHECK_FALSE(activated.value()->displayName(target));
      CHECK(read(unit / "catalog-source.txt") == oldFragment);
      CHECK(read(unit / "source.png") == kLinearPngBytes);
      CHECK(read(inbox) == "rename must not use the inbox");
      if (rename != Rename::Success && rename != Rename::Confirmed && rename != Rename::StagedPrevious) {
        REQUIRE(renamed.state == ca::RebuildState::Failed);
        REQUIRE(renamed.failure);
        CHECK_FALSE(renamed.generation);
        const bool unconfirmed = rename == Rename::Unknown || rename == Rename::FinalHeader;
        CHECK(fs::exists(names) == unconfirmed);
        CHECK(fs::exists(f.marker) == unconfirmed);
        CHECK(publishedCount() == priorCount + (unconfirmed ? 1 : 0));
        if (rename == Rename::ActiveHeader || rename == Rename::PreparedHeader)
          CHECK(renamed.failure->code == ca::RebuildFailureCode::InvalidOutput);
        if (rename == Rename::StagedNext || rename == Rename::LinkedNext)
          CHECK(renamed.failure->code == ca::RebuildFailureCode::PublishFailed);
        if (rename == Rename::LinkedNext) CHECK(read(linkedName).find("display_alias") != std::string::npos);
        if (rename == Rename::LateTimeout) {
          CHECK(reachedDeadline);
          CHECK(renamed.failure->code == ca::RebuildFailureCode::TimedOut);
        }
        if (unconfirmed) {
          if (rename == Rename::FinalHeader) write(oldRoot / inventory, originalInventory);
          CHECK(activated.value()->renameTexture({target, previousGeneration, "again"}, snapshot).error() ==
                ca::RenameTextureError::WorkspaceNeedsRepair);
          activated.value().reset();
          auto fallback = AssetRebuildHost::open(config);
          REQUIRE(fallback.isOk());
          CHECK_FALSE(fallback.value()->displayName(target));
          CHECK(fallback.value()->admissionError() == ca::RebuildError::WorkspaceNeedsRepair);
        }
        return;
      }
      REQUIRE(renamed.state == ca::RebuildState::Succeeded);
      REQUIRE(renamed.generation);
      const auto renamedRoot = f.published / as::toString(renamed.generation->value);
      CHECK(read(renamedRoot / "asset_catalog.txt") == read(oldRoot / "asset_catalog.txt"));
      CHECK(read(renamedRoot / "artifact") == read(oldRoot / "artifact"));
      CHECK(read(renamedRoot / "generation.json").find("\"version\":8") != std::string::npos);
      CHECK(activated.value()->renameTexture({target, previousGeneration, "again"}, snapshot).error() ==
            ca::RenameTextureError::StaleSnapshot);
      CHECK(activated.value()->startTextureImport("unused", ca::ImportMode::Resume, snapshot).error() ==
            ca::ImportError::ActivationRequired);
      activated.value().reset();
      config.generation = renamed.generation;
      config.beforeRenamePublication = {};
      config.afterRenamePreparation = {};
      fs::path rollbackRecord;
      std::string exchangedNames;
      if (rename == Rename::StagedPrevious) {
        config.beforeRenameExchange = [&](const fs::path& pending, auto) { rollbackRecord = pending; };
        config.beforeRenamePublication = [&](const fs::path& prepared, const fs::path&) {
          exchangedNames = read(names);
          if (!(std::ofstream(rollbackRecord / "previous", std::ios::binary) << "altered backup"))
            throw std::runtime_error("Cannot change staged backup bytes");
          changeHeader(prepared);
        };
      }
      auto newer = AssetRebuildHost::open(config);
      INFO((newer.isErr() ? newer.error() : std::string{}));
      REQUIRE(newer.isOk());
      CHECK(newer.value()->displayName(target) == "display_alias");
      CHECK(newer.value()->startTextureImport("display_alias", ca::ImportMode::Register, snapshot).error() ==
            ca::ImportError::NameConflict);
      CHECK(newer.value()->renameTexture({target, previousGeneration, "again"}, snapshot).error() ==
            ca::RenameTextureError::StaleSnapshot);
      if (rename == Rename::StagedPrevious) {
        auto current = as::loadAssetCatalog(newer.value()->catalogPath().generic_string());
        REQUIRE(current.isOk());
        const auto request = newer.value()->renameTexture({target, *renamed.generation, "next_alias"},
            std::make_shared<const as::AssetCatalog>(std::move(current.value())));
        REQUIRE(request.isOk());
        const auto failed = f.complete(*newer.value(), request.value().id);
        CHECK(failed.state == ca::RebuildState::Failed);
        CHECK_FALSE(failed.generation);
        CHECK(newer.value()->displayName(target) == "display_alias");
        CHECK(exchangedNames.find("next_alias") != std::string::npos);
        CHECK(read(names) == exchangedNames);
        CHECK(fs::exists(rollbackRecord / "rename.json"));
        CHECK(read(rollbackRecord / "previous") == "altered backup");
        CHECK(fs::exists(f.marker));
        CHECK(publishedCount() == priorCount + 2);  // The verified generation and private candidate.
        CHECK(newer.value()->admissionError() == ca::RebuildError::WorkspaceNeedsRepair);
        return;
      }
      if (rename == Rename::Success) {
        auto fromNew = as::loadAssetCatalog(newer.value()->catalogPath().generic_string());
        REQUIRE(fromNew.isOk());
        const auto current = std::make_shared<const as::AssetCatalog>(std::move(fromNew.value()));
        write(inbox, kReplacementBytes);
        write(f.staging / "artifact", read(oldRoot / "artifact"));
        const auto reimported = newer.value()->start(target, current);
        REQUIRE(reimported.isOk());
        const auto updated = f.complete(*newer.value(), reimported.value().id);
        INFO((updated.failure ? updated.failure->diagnostic : "no reimport failure"));
        REQUIRE(updated.state == ca::RebuildState::Succeeded);
        REQUIRE(updated.generation);
        CHECK(read(unit / "source.png") == kReplacementBytes);
        newer.value().reset();
        config.generation = updated.generation;
        newer = AssetRebuildHost::open(config);
        REQUIRE(newer.isOk());
        CHECK(newer.value()->displayName(target) == "display_alias");
        auto updatedCatalog = as::loadAssetCatalog(newer.value()->catalogPath().generic_string());
        REQUIRE(updatedCatalog.isOk());
        const auto back = newer.value()->renameTexture({target, *updated.generation, "sample"},
            std::make_shared<const as::AssetCatalog>(std::move(updatedCatalog.value())));
        REQUIRE(back.isOk());
        const auto restored = f.complete(*newer.value(), back.value().id);
        REQUIRE(restored.state == ca::RebuildState::Succeeded);
        REQUIRE(restored.generation);
        CHECK_FALSE(fs::exists(names));
        CHECK(read(f.published / as::toString(restored.generation->value) / "generation.json").find("textureDisplayNames") ==
              std::string::npos);
      }
      return;
    }
    if (reimport == Replacement::None) return;
    const auto request = [&] {
      const auto started = activated.value()->start(target, snapshot);
      REQUIRE(started.isOk());
      return f.complete(*activated.value(), started.value().id);
    };
    if (reimport == Replacement::InvalidRetry) {
      write(inbox, "not a PNG");
      const auto failed = request();
      REQUIRE(failed.state == ca::RebuildState::Failed);
      REQUIRE_FALSE(failed.generation);
      CHECK(read(unit / "source.png") == kLinearPngBytes);
      CHECK(read(unit / "catalog-source.txt") == oldFragment);
      CHECK_FALSE(fs::exists(f.marker));
      CHECK_FALSE(fs::exists(project / ".atlantis-import-dirty"));
      CHECK(read(f.staging / "assets/managed-pending.txt").empty());
    }
    write(inbox, kReplacementBytes);
    const auto replaced = request();
    if (blockedPublish != INVALID_HANDLE_VALUE) CloseHandle(blockedPublish);
    INFO((replaced.failure ? replaced.failure->diagnostic : "no replacement failure"));
    CHECK(replaced.asset == target);
    CHECK(read(unit / "catalog-source.txt") == oldFragment);
    CHECK(read(oldRoot / "assets/managed-texture-sources/texture_sample/source.png") == kLinearPngBytes);
    if (reimport == Replacement::Tamper || reimport == Replacement::Exchange ||
        reimport == Replacement::PublishRefused || reimport == Replacement::Unknown) {
      REQUIRE(replaced.state == ca::RebuildState::Failed);
      REQUIRE_FALSE(replaced.generation);
      if (reimport == Replacement::Unknown) {
        CHECK(fs::exists(f.marker));
        CHECK(fs::exists(project / ".atlantis-import-dirty"));
        CHECK(read(unit / "source.png") == kReplacementBytes);
        const auto held = activated.value()->start(target, snapshot);
        REQUIRE(held.isErr());
        CHECK(held.error() == ca::RebuildError::WorkspaceNeedsRepair);
        activated.value().reset();
        const auto fallback = AssetRebuildHost::open(config);
        REQUIRE(fallback.isOk());
        CHECK(fs::equivalent(fallback.value()->catalogPath(), oldRoot / "asset_catalog.txt"));
      } else {
        CHECK(read(unit / "source.png") == kLinearPngBytes);
        const bool cleanupHeld = reimport == Replacement::PublishRefused;
        CHECK(fs::exists(f.marker) == cleanupHeld);
        CHECK(fs::exists(project / ".atlantis-import-dirty") == cleanupHeld);
        for (const auto& entry : fs::directory_iterator(f.published))
          if (entry.is_directory() && !entry.path().filename().string().starts_with(".pending-"))
            CHECK(entry.path() == oldRoot);
      }
      return;
    }
    REQUIRE(replaced.state == ca::RebuildState::Succeeded);
    REQUIRE(replaced.generation);
    CHECK(replaced.generation != previousGeneration);
    CHECK(read(unit / "source.png") == kReplacementBytes);
    if (reimport == Replacement::Frozen) CHECK(read(inbox) == "a newer inbox edit");
    CHECK(fs::equivalent(activated.value()->catalogPath(), oldRoot / "asset_catalog.txt"));
    const auto ahead = activated.value()->start(target, snapshot);
    REQUIRE(ahead.isErr());
    CHECK(ahead.error() == ca::RebuildError::Unavailable);
    activated.value().reset();
    config.generation = replaced.generation;
    const auto updated = AssetRebuildHost::open(config);
    INFO((updated.isErr() ? updated.error() : std::string{}));
    REQUIRE(updated.isOk());
    auto freshCatalog = as::loadAssetCatalog(updated.value()->catalogPath().generic_string());
    REQUIRE(freshCatalog.isOk());
    REQUIRE(freshCatalog.value().find(target));
    CHECK(as::toString(freshCatalog.value().find(target)->source) == "content:texture_sample/source.png");
  } else {
    REQUIRE(job.state == ca::RebuildState::Failed);
    REQUIRE(job.failure);
    CHECK(job.failure->stage == stage);
    CHECK(job.failure->code == code);
    CHECK(job.failure->registration == (moved ? ca::RegistrationState::Committed : ca::RegistrationState::NotCommitted));
    CHECK_FALSE(job.generation);
    if (moved) CHECK(status.value().workspaceNeedsRepair);
    else for (const auto& file : fs::directory_iterator(f.published))
      CHECK_FALSE((file.is_directory() && !file.path().filename().string().starts_with(".pending-")));
  }
}
