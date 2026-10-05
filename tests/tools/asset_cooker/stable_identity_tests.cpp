#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/decode_scene.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Plan 0047 M9 (R14, Spec 0047): renaming or moving a source changes no
// artifact byte and breaks no reference. The REAL atlantis_asset_cooker runs
// as a subprocess over a temporary tree: a mesh, a texture, a material and
// two scenes sharing the mesh are cooked and assembled; the mesh and texture
// sources are then renamed and moved, only their two catalog-source lines are
// edited, and the tree is cooked and assembled again. GPU-independent.

namespace {

namespace as = atlantis::asset_system;
namespace fs = std::filesystem;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_stable_identity_tests" /
             (label + "_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

// The identities the tree's references name. They live in the catalog source,
// never in a source path -- which is the point of the test.
constexpr const char* kMeshGuid = "0047aaaa-0000-4000-8000-0000000000a1";
constexpr const char* kTextureGuid = "0047aaaa-0000-4000-8000-0000000000a2";
constexpr const char* kMaterialGuid = "0047aaaa-0000-4000-8000-0000000000a3";
constexpr const char* kSceneAGuid = "0047aaaa-0000-4000-8000-0000000000b1";
constexpr const char* kSceneBGuid = "0047aaaa-0000-4000-8000-0000000000b2";

constexpr const char* kMaterialPath = "materials/m.material.txt";
constexpr const char* kSceneAPath = "scenes/a.scene.txt";
constexpr const char* kSceneBPath = "scenes/b.scene.txt";

[[nodiscard]] as::AssetGuid guid(const char* text) { return as::parseAssetGuid(text).value(); }

struct Layout {
  std::string mesh;
  std::string texture;
};

const Layout kBefore{"meshes/m.mesh.txt", "textures/t.dds"};
const Layout kAfter{"props/renamed/crate.mesh.txt", "surfaces/moved_t.dds"};

[[nodiscard]] std::string quote(const std::string& s) { return "\"" + s + "\""; }

void writeText(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Runs the real cooker with `arguments`; stderr is captured into
// `errorFile` (when given). The command is wrapped in one extra quote pair for
// cmd.exe (see cooker_determinism_tests.cpp).
[[nodiscard]] int runCooker(const std::vector<std::string>& arguments, const fs::path& errorFile = {}) {
  std::string command = quote(ATLANTIS_ASSET_COOKER_EXECUTABLE);
  for (const std::string& argument : arguments) {
    const std::size_t eq = argument.find('=');
    command += " " + (eq == std::string::npos ? argument
                                              : argument.substr(0, eq + 1) + quote(argument.substr(eq + 1)));
  }
  if (!errorFile.empty()) command += " 2> " + quote(errorFile.string());
  return std::system(quote(command).c_str());
}

[[nodiscard]] std::string catalogSourceText(const Layout& layout) {
  std::vector<as::CatalogSourceEntry> entries = {
      {guid(kMeshGuid), as::CatalogAssetType::Mesh, as::CatalogRoot::Assets, layout.mesh},
      {guid(kTextureGuid), as::CatalogAssetType::Texture, as::CatalogRoot::Assets, layout.texture},
      {guid(kMaterialGuid), as::CatalogAssetType::Material, as::CatalogRoot::Assets, kMaterialPath},
      {guid(kSceneAGuid), as::CatalogAssetType::Scene, as::CatalogRoot::Assets, kSceneAPath},
      {guid(kSceneBGuid), as::CatalogAssetType::Scene, as::CatalogRoot::Assets, kSceneBPath},
  };
  return as::serializeAssetCatalogSource(std::move(entries));
}

void writeSources(const fs::path& assets, const Layout& layout) {
  writeText(assets / layout.mesh,
            "atlantis_static_mesh_source_version: 3\n"
            "vertex_count: 3\n"
            "index_count: 3\n"
            "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
            "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
            "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
            "index: 0 1 2\n");
  fs::create_directories((assets / layout.texture).parent_path());
  fs::copy_file(fs::path(ATLANTIS_ASSET_ROOT) / "textures" / "paris_stringlights_diff.dds", assets / layout.texture,
                fs::copy_options::overwrite_existing);
  writeText(assets / kMaterialPath, std::string("atlantis_material_source_version: 10\n"
                                                "kind: unlit_textured\n"
                                                "texture: ") +
                                        kTextureGuid +
                                        "\n"
                                        "filter: linear\n"
                                        "address_mode: repeat\n");
  const auto scene = [](const char* nodeGuid) {
    return std::string("atlantis_scene_source_version: 7\n"
                       "node_count: 1\n"
                       "active_camera: none\n"
                       "node: node_id=1 guid=") +
           nodeGuid + " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" + kMeshGuid +
           " material=" + kMaterialGuid + "\n";
  };
  writeText(assets / kSceneAPath, scene("e68122c6-1bb2-8f1f-b185-358f58780b05"));
  writeText(assets / kSceneBPath, scene("e68122c6-18b2-8f1f-b185-358f58780754"));
}

// One cooked-and-assembled state of the tree.
struct CookedState {
  fs::path out;
  fs::path catalog;
  fs::path meshArtifact;
  fs::path textureArtifact;
  fs::path materialArtifact;
  fs::path sceneAArtifact;
  fs::path sceneBArtifact;
  std::vector<std::string> fragments;
  int meshCookExit = -1;
};

[[nodiscard]] std::string withoutSuffix(std::string text, std::string_view suffix) {
  REQUIRE(text.size() > suffix.size());
  text.resize(text.size() - suffix.size());
  return text;
}

// Cooks every asset of `assets` (currently laid out as `layout`) against
// `catalogSource` into `out`. The mesh cook's result is returned in
// meshCookExit, so a caller can expect it to fail.
[[nodiscard]] CookedState cookAll(const fs::path& assets, const Layout& layout, const fs::path& catalogSource,
                                  const fs::path& out, const fs::path& errorFile = {}) {
  CookedState state;
  state.out = out;
  const std::string meshBase = withoutSuffix(layout.mesh, ".mesh.txt");
  const std::string textureStem = fs::path(layout.texture).stem().string();
  state.meshArtifact = out / (meshBase + ".amesh");
  state.textureArtifact = out / (textureStem + ".atex");
  state.materialArtifact = out / "materials/m.amaterial";
  state.sceneAArtifact = out / "scenes/a.ascene";
  state.sceneBArtifact = out / "scenes/b.ascene";

  const auto common = [&](const std::string& kind, const std::string& source) {
    return std::vector<std::string>{"--kind=" + kind, "--source=" + (assets / source).generic_string(),
                                    "--asset-root=" + assets.generic_string(),
                                    "--output-dir=" + out.generic_string(),
                                    "--catalog-source=" + catalogSource.generic_string()};
  };
  state.meshCookExit = runCooker(common("mesh", layout.mesh), errorFile);
  if (state.meshCookExit != 0) return state;
  state.fragments.push_back(state.meshArtifact.string() + ".catalog.txt");

  auto texture = common("texture", layout.texture);
  texture.push_back("--stamp=" + (out / (textureStem + ".stamp")).generic_string());
  REQUIRE(runCooker(texture) == 0);
  state.fragments.push_back(state.textureArtifact.string() + ".catalog.txt");

  REQUIRE(runCooker(common("material", kMaterialPath)) == 0);
  state.fragments.push_back(state.materialArtifact.string() + ".catalog.txt");
  REQUIRE(runCooker(common("scene", kSceneAPath)) == 0);
  state.fragments.push_back(state.sceneAArtifact.string() + ".catalog.txt");
  REQUIRE(runCooker(common("scene", kSceneBPath)) == 0);
  state.fragments.push_back(state.sceneBArtifact.string() + ".catalog.txt");
  return state;
}

[[nodiscard]] std::string declarationsText(const Layout& layout) {
  return "mesh\tassets\t" + layout.mesh + "\ntexture\tassets\t" + layout.texture + "\nmaterial\tassets\t" +
         kMaterialPath + "\nscene\tassets\t" + kSceneAPath + "\nscene\tassets\t" + kSceneBPath + "\n";
}

// Assembles `fragments` into `catalogPath` (whose directory must contain
// every artifact); returns the exit code.
[[nodiscard]] int assemble(const fs::path& workDir, const fs::path& catalogSource, const Layout& layout,
                           const std::vector<std::string>& fragments, const fs::path& catalogPath,
                           const fs::path& errorFile = {}) {
  std::string fragmentList;
  for (const std::string& fragment : fragments) fragmentList += fragment + "\n";
  writeText(workDir / "fragments.txt", fragmentList);
  writeText(workDir / "declarations.txt", declarationsText(layout));
  return runCooker({"--kind=assemble-catalog", "--catalog-source=" + catalogSource.generic_string(),
                    "--declarations=" + (workDir / "declarations.txt").generic_string(),
                    "--fragment-list=" + (workDir / "fragments.txt").generic_string(),
                    "--out=" + catalogPath.generic_string()},
                   errorFile);
}

// Every line of a sidecar except the source-path one.
[[nodiscard]] std::string sidecarWithoutSourcePath(const fs::path& path) {
  std::istringstream in(readText(path));
  std::string kept;
  for (std::string line; std::getline(in, line);) {
    if (line.rfind("source_logical_path: ", 0) == 0) continue;
    kept += line + "\n";
  }
  return kept;
}

[[nodiscard]] std::map<std::string, as::AssetCatalogRecord> recordsByGuid(const fs::path& catalogPath) {
  const auto parsed = as::parseAssetCatalogRecords(readText(catalogPath));
  REQUIRE(parsed.isOk());
  std::map<std::string, as::AssetCatalogRecord> byGuid;
  for (const as::AssetCatalogRecord& record : parsed.value()) byGuid.emplace(as::toString(record.guid), record);
  return byGuid;
}

}  // namespace

TEST_CASE("Renaming and moving a mesh and a texture source, editing only their two catalog lines, changes no "
          "artifact byte and every reference still resolves",
          "[asset_cooker][stable_identity][tool]") {
  TempDirGuard dir("rename_move");
  const fs::path assets = dir.path / "assets";
  const fs::path catalogSourceBefore = dir.path / "catalog_source_before.txt";
  const fs::path catalogSourceAfter = dir.path / "catalog_source_after.txt";

  // Before: cook and assemble.
  writeSources(assets, kBefore);
  writeText(catalogSourceBefore, catalogSourceText(kBefore));
  const CookedState before = cookAll(assets, kBefore, catalogSourceBefore, dir.path / "out_before");
  REQUIRE(before.meshCookExit == 0);
  const fs::path catalogBefore = dir.path / "out_before" / "asset_catalog.txt";
  REQUIRE(assemble(dir.path / "work_before", catalogSourceBefore, kBefore, before.fragments, catalogBefore) == 0);

  // Rename and move the two sources; the catalog source changes by exactly
  // their two lines.
  fs::create_directories((assets / kAfter.mesh).parent_path());
  fs::rename(assets / kBefore.mesh, assets / kAfter.mesh);
  fs::create_directories((assets / kAfter.texture).parent_path());
  fs::rename(assets / kBefore.texture, assets / kAfter.texture);
  writeText(catalogSourceAfter, catalogSourceText(kAfter));
  {
    const std::string a = readText(catalogSourceBefore);
    const std::string b = readText(catalogSourceAfter);
    std::istringstream inA(a);
    std::istringstream inB(b);
    std::size_t differingLines = 0;
    std::string lineA;
    std::string lineB;
    while (std::getline(inA, lineA) && std::getline(inB, lineB)) differingLines += lineA != lineB ? 1 : 0;
    CHECK(differingLines == 2);
  }

  // After: cook and assemble again.
  const CookedState after = cookAll(assets, kAfter, catalogSourceAfter, dir.path / "out_after");
  REQUIRE(after.meshCookExit == 0);
  const fs::path catalogAfter = dir.path / "out_after" / "asset_catalog.txt";
  REQUIRE(assemble(dir.path / "work_after", catalogSourceAfter, kAfter, after.fragments, catalogAfter) == 0);

  // Every artifact is byte-identical.
  CHECK(readText(before.meshArtifact) == readText(after.meshArtifact));
  CHECK(readText(before.textureArtifact) == readText(after.textureArtifact));
  CHECK(readText(before.materialArtifact) == readText(after.materialArtifact));
  CHECK(readText(before.sceneAArtifact) == readText(after.sceneAArtifact));
  CHECK(readText(before.sceneBArtifact) == readText(after.sceneBArtifact));
  CHECK(readText(before.meshArtifact).size() > 0);

  // Sidecars differ only in the recorded source path of the two moved assets.
  CHECK(sidecarWithoutSourcePath(before.meshArtifact.string() + ".meta.txt") ==
        sidecarWithoutSourcePath(after.meshArtifact.string() + ".meta.txt"));
  CHECK(sidecarWithoutSourcePath(before.textureArtifact.string() + ".meta.txt") ==
        sidecarWithoutSourcePath(after.textureArtifact.string() + ".meta.txt"));
  CHECK(readText(before.materialArtifact.string() + ".meta.txt") == readText(after.materialArtifact.string() + ".meta.txt"));
  CHECK(readText(before.sceneAArtifact.string() + ".meta.txt") == readText(after.sceneAArtifact.string() + ".meta.txt"));
  CHECK(readText(before.sceneBArtifact.string() + ".meta.txt") == readText(after.sceneBArtifact.string() + ".meta.txt"));

  // The catalog changed only in the moved assets' source and location fields.
  const auto recordsBefore = recordsByGuid(catalogBefore);
  const auto recordsAfter = recordsByGuid(catalogAfter);
  REQUIRE(recordsBefore.size() == 5);
  REQUIRE(recordsAfter.size() == 5);
  for (const auto& [guidText, recordBefore] : recordsBefore) {
    INFO(guidText);
    REQUIRE(recordsAfter.contains(guidText));
    const as::AssetCatalogRecord& recordAfter = recordsAfter.at(guidText);
    const bool moved = guidText == kMeshGuid || guidText == kTextureGuid;
    if (!moved) {
      CHECK(as::formatAssetCatalogRecord(recordBefore) == as::formatAssetCatalogRecord(recordAfter));
      continue;
    }
    as::AssetCatalogRecord normalizedAfter = recordAfter;
    normalizedAfter.source = recordBefore.source;
    normalizedAfter.artifact = recordBefore.artifact;
    normalizedAfter.metadata = recordBefore.metadata;
    CHECK(as::formatAssetCatalogRecord(recordBefore) == as::formatAssetCatalogRecord(normalizedAfter));
    CHECK(recordAfter.source.path == (guidText == std::string(kMeshGuid) ? kAfter.mesh : kAfter.texture));
    CHECK(recordAfter.artifact != recordBefore.artifact);
  }

  // Both scenes still resolve: each depends on the same mesh and material
  // records, and decodes through its catalog record to the same asset keys.
  const auto catalog = as::loadAssetCatalog(catalogAfter.string());
  REQUIRE(catalog.isOk());
  const as::AssetCatalogRecord* mesh = catalog.value().find(guid(kMeshGuid));
  const as::AssetCatalogRecord* material = catalog.value().find(guid(kMaterialGuid));
  REQUIRE(mesh != nullptr);
  REQUIRE(material != nullptr);
  CHECK(mesh->type == as::CatalogAssetType::Mesh);
  CHECK(material->type == as::CatalogAssetType::Material);
  CHECK(material->dependencies == std::vector<as::AssetGuid>{guid(kTextureGuid)});
  std::vector<as::AssetGuid> expectedDependencies = {guid(kMeshGuid), guid(kMaterialGuid)};
  std::sort(expectedDependencies.begin(), expectedDependencies.end());
  for (const char* sceneGuidText : {kSceneAGuid, kSceneBGuid}) {
    INFO(sceneGuidText);
    const as::AssetCatalogRecord* scene = catalog.value().find(guid(sceneGuidText));
    REQUIRE(scene != nullptr);
    CHECK(scene->dependencies == expectedDependencies);
    for (const as::AssetGuid& dependency : scene->dependencies) CHECK(catalog.value().find(dependency) != nullptr);
    const auto decoded = as::decodeScene(scene->artifact, scene->metadata);
    REQUIRE(decoded.isOk());
    REQUIRE(decoded.value().nodeCount() == 1);
    CHECK(decoded.value().node(0).renderable->meshAsset == mesh->assetId);
    CHECK(decoded.value().node(0).renderable->materialAsset == material->assetId);
  }
}

TEST_CASE("A moved source without its catalog edit fails as SourceNotInCatalog", "[asset_cooker][stable_identity][tool]") {
  TempDirGuard dir("move_without_edit");
  const fs::path assets = dir.path / "assets";
  writeSources(assets, kBefore);
  writeText(dir.path / "catalog_source.txt", catalogSourceText(kBefore));

  // Move the mesh source but leave the catalog source alone.
  fs::create_directories((assets / kAfter.mesh).parent_path());
  fs::rename(assets / kBefore.mesh, assets / kAfter.mesh);

  const fs::path errorFile = dir.path / "stderr.txt";
  const int exitCode = runCooker({"--kind=mesh", "--source=" + (assets / kAfter.mesh).generic_string(),
                                  "--asset-root=" + assets.generic_string(),
                                  "--output-dir=" + (dir.path / "out").generic_string(),
                                  "--catalog-source=" + (dir.path / "catalog_source.txt").generic_string()},
                                 errorFile);
  CHECK(exitCode != 0);
  CHECK(readText(errorFile).find("SourceNotInCatalog") != std::string::npos);
  CHECK_FALSE(fs::exists(dir.path / "out" / (withoutSuffix(kAfter.mesh, ".mesh.txt") + ".amesh")));
}

TEST_CASE("A copied source that keeps the original GUID fails: the catalog source rejects it, and assembly reports "
          "DuplicateGuid",
          "[asset_cooker][stable_identity][tool]") {
  TempDirGuard dir("copy_keeps_guid");
  const fs::path assets = dir.path / "assets";
  writeSources(assets, kBefore);
  writeText(dir.path / "catalog_source.txt", catalogSourceText(kBefore));
  const CookedState original = cookAll(assets, kBefore, dir.path / "catalog_source.txt", dir.path / "out_original");
  REQUIRE(original.meshCookExit == 0);

  const std::string copyPath = "meshes/copy.mesh.txt";
  fs::copy_file(assets / kBefore.mesh, assets / copyPath);

  // (1) The copy given its own catalog line under the original's GUID: two
  //     entries, one GUID -- the catalog source itself is invalid.
  {
    std::vector<as::CatalogSourceEntry> entries = {
        {guid(kMeshGuid), as::CatalogAssetType::Mesh, as::CatalogRoot::Assets, kBefore.mesh},
        {guid(kMeshGuid), as::CatalogAssetType::Mesh, as::CatalogRoot::Assets, copyPath},
    };
    writeText(dir.path / "catalog_source_two_lines.txt", as::serializeAssetCatalogSource(std::move(entries)));
    const fs::path errorFile = dir.path / "stderr_two_lines.txt";
    const int exitCode = runCooker({"--kind=mesh", "--source=" + (assets / copyPath).generic_string(),
                                    "--asset-root=" + assets.generic_string(),
                                    "--output-dir=" + (dir.path / "out_copy_a").generic_string(),
                                    "--catalog-source=" + (dir.path / "catalog_source_two_lines.txt").generic_string()},
                                   errorFile);
    CHECK(exitCode != 0);
    CHECK(readText(errorFile).find("duplicate GUID") != std::string::npos);
  }

  // (2) The copy cooked as the GUID's new location while the original's
  //     cooked output is still in the build: two fragments carry one GUID, so
  //     assembly fails as DuplicateGuid (not a silent pick of one).
  {
    std::vector<as::CatalogSourceEntry> entries = {
        {guid(kMeshGuid), as::CatalogAssetType::Mesh, as::CatalogRoot::Assets, copyPath},
        {guid(kTextureGuid), as::CatalogAssetType::Texture, as::CatalogRoot::Assets, kBefore.texture},
        {guid(kMaterialGuid), as::CatalogAssetType::Material, as::CatalogRoot::Assets, kMaterialPath},
        {guid(kSceneAGuid), as::CatalogAssetType::Scene, as::CatalogRoot::Assets, kSceneAPath},
        {guid(kSceneBGuid), as::CatalogAssetType::Scene, as::CatalogRoot::Assets, kSceneBPath},
    };
    writeText(dir.path / "catalog_source_copy.txt", as::serializeAssetCatalogSource(std::move(entries)));
    REQUIRE(runCooker({"--kind=mesh", "--source=" + (assets / copyPath).generic_string(),
                       "--asset-root=" + assets.generic_string(),
                       "--output-dir=" + (dir.path / "out_copy_b").generic_string(),
                       "--catalog-source=" + (dir.path / "catalog_source_copy.txt").generic_string()}) == 0);

    std::vector<std::string> fragments = original.fragments;
    fragments.push_back((dir.path / "out_copy_b" / "meshes/copy.amesh.catalog.txt").string());
    const fs::path errorFile = dir.path / "stderr_assemble.txt";
    CHECK(assemble(dir.path / "work_copy", dir.path / "catalog_source_copy.txt", kBefore, fragments,
                   dir.path / "dup_catalog.txt", errorFile) != 0);
    CHECK(readText(errorFile).find("DuplicateGuid") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path / "dup_catalog.txt"));
  }
}
