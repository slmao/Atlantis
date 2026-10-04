#include "gltf_test_builder.h"
#include "import_command.h"
#include "material_import.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>

namespace {

// Plan 0047 P7: the GUID a test import's root takes in place of a catalog
// lookup.
[[nodiscard]] atlantis::asset_system::AssetGuid testImportRoot() {
  return atlantis::asset_system::parseAssetGuid("0047eeee-0000-4000-8000-000000000001").value();
}

}  // namespace
// Plan 0046 Milestone 2 (ADR-0094 Decision 2, Plan 0046 P8): atlantis_asset_cooker
// --kind=cook-manifest over a synthetic import -- the real importer writes
// the import directory, the real cooker binary executes its cook_manifest.txt
// in one process and writes the Runtime dependency manifest.

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;

namespace {

std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::byte> readBytes(const fs::path& path) {
  const std::string text = readText(path);
  return std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                reinterpret_cast<const std::byte*>(text.data()) + text.size());
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) out.push_back(line);
  }
  return out;
}

// Each --flag=value argument's value quoted for cmd.exe, and the whole
// command quoted once more (the std::system() rule bistro_end_to_end_tests.cpp
// documents).
int runCooker(const std::vector<std::string>& arguments) {
  std::string command = "\"" + std::string(ATLANTIS_ASSET_COOKER_EXECUTABLE) + "\"";
  for (const std::string& argument : arguments) {
    const std::size_t eq = argument.find('=');
    command += " " + (eq == std::string::npos ? argument
                                              : argument.substr(0, eq + 1) + "\"" + argument.substr(eq + 1) + "\"");
  }
  return std::system(("\"" + command + "\"").c_str());
}

struct ImportedFixture {
  fs::path dir;        // the content root: the glTF and its DDS files
  fs::path importDir;  // the importer's output
  fs::path cookedDir;
};

// A quad with one material: a DDS base colour and a DDS emissive mask, so the
// manifest carries two texture lines, one material line and the scene line.
ImportedFixture importFixture(const std::string& testName) {
  ImportedFixture fixture;
  fixture.dir = gltf_test::freshDirectory(testName);
  // Tagged BC7_UNORM (DXGI 99), like Bistro's files: the colour-used ones
  // must still cook as sRGB (Spec 0046 Q8).
  std::vector<unsigned char> dds = atlantis::gltf_importer::detail::whiteFallbackDds();
  dds[128] = 99;
  for (const char* file : {"quad_diff.dds", "quad_em.dds"}) {
    std::ofstream(fixture.dir / file, std::ios::binary)
        .write(reinterpret_cast<const char*>(dds.data()), static_cast<std::streamsize>(dds.size()));
  }
  auto spec = gltf_test::unitQuad();
  spec.imagesJson = "[{\"uri\":\"missing.png\"},{\"uri\":\"quad_diff.dds\"},{\"uri\":\"quad_em.dds\"}]";
  spec.texturesJson =
      "[{\"source\":0,\"extensions\":{\"MSFT_texture_dds\":{\"source\":1}}},"
      "{\"source\":0,\"extensions\":{\"MSFT_texture_dds\":{\"source\":2}}}]";
  spec.extensionsUsedJson = "[\"MSFT_texture_dds\"]";
  spec.materialsJson =
      "[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},\"emissiveFactor\":[4.0,4.0,4.0],"
      "\"emissiveTexture\":{\"index\":1}}]";
  const fs::path input = gltf_test::writeGltf(fixture.dir, spec);
  fixture.importDir = fixture.dir.parent_path() / (testName + "_import");
  fixture.cookedDir = fixture.dir.parent_path() / (testName + "_cooked");
  fs::remove_all(fixture.importDir);
  fs::remove_all(fixture.cookedDir);
  const auto result = atlantis::gltf_importer::importGltf(input, fixture.dir, fixture.importDir, "q", testImportRoot());
  REQUIRE(result.isOk());
  return fixture;
}

std::vector<std::string> cookManifestArguments(const ImportedFixture& fixture) {
  return {"--kind=cook-manifest", "--import-dir=" + fixture.importDir.generic_string(),
          "--cooked-dir=" + fixture.cookedDir.generic_string(),
          "--content-parent=" + fixture.dir.parent_path().generic_string(),
          "--stamp=" + (fixture.cookedDir / "q.stamp").generic_string()};
}

}  // namespace

TEST_CASE("cook-manifest mode cooks every manifest line in one process, and every record of the import points at "
          "an existing artifact and sidecar",
          "[asset_cooker][cook_manifest]") {
  const ImportedFixture fixture = importFixture("cook_manifest_mode");
  REQUIRE(runCooker(cookManifestArguments(fixture)) == 0);
  CHECK(fs::exists(fixture.cookedDir / "q.stamp"));

  const auto scene = as::decodeSceneArtifact(readBytes(fixture.cookedDir / "q/q.ascene"));
  CHECK(scene.isOk());

  const auto records = as::parseAssetCatalogRecords(readText(fixture.importDir / "import.catalog.txt"));
  REQUIRE(records.isOk());
  REQUIRE(records.value().size() == 5);  // 1 mesh, 1 material, 2 textures, the scene
  std::set<as::AssetId> listedIds;
  std::size_t srgbTextures = 0;
  for (const as::AssetCatalogRecord& record : records.value()) {
    INFO(as::toString(record.guid));
    CHECK(fs::exists(record.artifact));
    CHECK(fs::exists(record.metadata));
    CHECK(listedIds.insert(record.assetId).second);
    // Meshes stay where the importer wrote them; everything else is cooked.
    const fs::path expectedRoot = record.type == as::CatalogAssetType::Mesh ? fixture.importDir : fixture.cookedDir;
    CHECK(fs::path(record.artifact).generic_string().rfind(expectedRoot.generic_string(), 0) == 0);
    // Spec 0046 Q8: both colour-used DXGI 99 textures cooked as sRGB.
    if (record.type == as::CatalogAssetType::Texture) {
      const auto texture = as::decodeTextureArtifact(readBytes(record.artifact));
      REQUIRE(texture.isOk());
      srgbTextures += texture.value().colorSpace == as::TextureColorSpace::Srgb ? 1 : 0;
    }
  }
  CHECK(srgbTextures == 2);

  // The material names both textures, and both have records.
  const auto material = as::decodeMaterialArtifact(readBytes(fixture.cookedDir / "q/materials/0.amaterial"));
  REQUIRE(material.isOk());
  CHECK(listedIds.count(material.value().textureAsset) == 1);
  CHECK(material.value().emissiveTexture != 0);
  CHECK(listedIds.count(material.value().emissiveTexture) == 1);
}

TEST_CASE("cook-manifest mode writes import.catalog.txt holding every record of the import, and it assembles",
          "[asset_cooker][cook_manifest][catalog]") {
  const ImportedFixture fixture = importFixture("cook_manifest_catalog");
  REQUIRE(runCooker(cookManifestArguments(fixture)) == 0);

  const fs::path importCatalog = fixture.importDir / "import.catalog.txt";
  const auto records = as::parseAssetCatalogRecords(readText(importCatalog));
  REQUIRE(records.isOk());
  const std::string rootPath = atlantis::gltf_importer::detail::importRootPath(fixture.dir / "input.gltf", fixture.dir);
  const auto guidOf = [](std::string_view subKey) { return as::deriveAssetGuid(testImportRoot(), subKey); };

  std::set<std::string> subKeys;
  for (const as::AssetCatalogRecord& record : records.value()) {
    INFO(as::formatAssetCatalogRecord(record));
    subKeys.insert(record.source.subKey);
    CHECK(record.source.root == as::CatalogRoot::Content);
    CHECK(record.source.path == rootPath);
    CHECK(record.guid == guidOf(record.source.subKey));
    CHECK(fs::path(record.artifact).is_absolute());
    CHECK(fs::exists(record.artifact));
    CHECK(fs::exists(record.metadata));
    if (record.type == as::CatalogAssetType::Mesh) {
      // Written by the importer itself: its {import_dir} is resolved.
      CHECK(record.artifact.rfind(fs::absolute(fixture.importDir).lexically_normal().generic_string(), 0) == 0);
      CHECK(record.tool == "atlantis-gltf-importer/1");
      CHECK(record.artifactSchema == 5);
    } else if (record.type == as::CatalogAssetType::Material) {
      std::vector<as::AssetGuid> expected = {guidOf("texture/quad_diff.dds"), guidOf("texture/quad_em.dds")};
      std::sort(expected.begin(), expected.end());
      CHECK(record.dependencies == expected);
    } else if (record.type == as::CatalogAssetType::Scene) {
      std::vector<as::AssetGuid> expected = {guidOf("mesh/0/0"), guidOf("material/0")};
      std::sort(expected.begin(), expected.end());
      CHECK(record.dependencies == expected);
    }
  }
  CHECK(subKeys == std::set<std::string>{"mesh/0/0", "texture/quad_diff.dds", "texture/quad_em.dds", "material/0",
                                         "scene"});

  // The import's one fragment assembles against a catalog source holding its
  // root, every record located relative to the catalog.
  const auto source = as::parseAssetCatalogSource(as::serializeAssetCatalogSource(
      {{testImportRoot(), as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, rootPath}}));
  REQUIRE(source.isOk());
  as::AssetCatalogAssemblyRequest request;
  request.catalogSource = &source.value();
  request.declarations = {{as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, rootPath}};
  request.fragmentPaths = {importCatalog.string()};
  request.outPath = (fixture.dir.parent_path() / "cook_manifest_catalog.catalog.txt").string();
  const auto assembled = as::assembleAssetCatalog(request);
  REQUIRE(assembled.isOk());
  CHECK(assembled.value().recordCount == 5);
}

TEST_CASE("cook-manifest mode fails the whole step on one failing line, and on a missing required flag",
          "[asset_cooker][cook_manifest]") {
  const ImportedFixture fixture = importFixture("cook_manifest_mode_failure");

  // A texture source that no longer exists fails its line, so the mode.
  fs::remove(fixture.dir / "quad_em.dds");
  CHECK(runCooker(cookManifestArguments(fixture)) != 0);
  CHECK_FALSE(fs::exists(fixture.importDir / "import.catalog.txt"));
  CHECK_FALSE(fs::exists(fixture.cookedDir / "q.stamp"));

  // The mode's own inputs are all required.
  std::vector<std::string> missingContentParent = cookManifestArguments(fixture);
  missingContentParent.erase(missingContentParent.begin() + 3);
  CHECK(runCooker(missingContentParent) != 0);
}

// Plan 0047 M5 (step 5): hand-authored scenes reference a mesh and a material
// of a builder-generated import. One assembled catalog resolves both scenes'
// references -- across the two roots -- to the import's single records.
TEST_CASE("Two hand-authored scenes referencing one import's mesh and material resolve to its single records "
          "through one assembled catalog",
          "[asset_cooker][cook_manifest][catalog]") {
  const ImportedFixture fixture = importFixture("cross_scene_catalog");
  REQUIRE(runCooker(cookManifestArguments(fixture)) == 0);

  const fs::path parent = fixture.dir.parent_path();
  const fs::path assetRoot = parent / "cross_scene_assets";
  const fs::path cookedScenes = parent / "cross_scene_cooked";
  fs::remove_all(assetRoot);
  fs::remove_all(cookedScenes);
  fs::create_directories(assetRoot / "scenes");

  const auto importGuid = [](std::string_view subKey) { return as::deriveAssetGuid(testImportRoot(), subKey); };
  const as::AssetGuid meshGuid = importGuid("mesh/0/0");
  const as::AssetGuid materialGuid = importGuid("material/0");
  const std::string rootPath = atlantis::gltf_importer::detail::importRootPath(fixture.dir / "input.gltf", fixture.dir);

  std::vector<as::CatalogSourceEntry> sourceEntries = {
      {testImportRoot(), as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, rootPath}};
  std::vector<as::AssetGuid> sceneGuids;
  std::vector<std::string> fragmentPaths = {(fixture.importDir / "import.catalog.txt").string()};
  as::AssetCatalogAssemblyRequest request;
  request.declarations = {{as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, rootPath}};
  for (const char* name : {"a", "b"}) {
    const as::AssetGuid sceneGuid =
        as::parseAssetGuid(std::string("0047eeee-0000-4000-8000-00000000000") + (name[0] == 'a' ? "a" : "b")).value();
    sceneGuids.push_back(sceneGuid);
    const std::string logical = std::string("scenes/") + name + ".scene.txt";
    sourceEntries.push_back({sceneGuid, as::CatalogAssetType::Scene, as::CatalogRoot::Assets, logical});
    request.declarations.push_back({as::CatalogAssetType::Scene, as::CatalogRoot::Assets, logical});
    std::ofstream(assetRoot / logical, std::ios::binary)
        << "atlantis_scene_source_version: 7\n"
           "node_count: 1\n"
           "active_camera: none\n"
           "node: node_id=1 guid=" << as::toString(as::deriveEntityGuid(sceneGuid, "node/1"))
        << " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" << as::toString(meshGuid)
        << " material=" << as::toString(materialGuid) << "\n";
  }
  const fs::path catalogSourcePath = parent / "cross_scene.catalog_source.txt";
  std::ofstream(catalogSourcePath, std::ios::binary) << as::serializeAssetCatalogSource(sourceEntries);
  for (const char* name : {"a", "b"}) {
    REQUIRE(runCooker({"--kind=scene", "--source=" + (assetRoot / "scenes" / (std::string(name) + ".scene.txt")).generic_string(),
                       "--asset-root=" + assetRoot.generic_string(), "--output-dir=" + cookedScenes.generic_string(),
                       "--stamp=" + (cookedScenes / (std::string(name) + ".stamp")).generic_string(),
                       "--catalog-source=" + catalogSourcePath.generic_string()}) == 0);
    fragmentPaths.push_back((cookedScenes / "scenes" / (std::string(name) + ".ascene.catalog.txt")).string());
  }

  const auto source = as::parseAssetCatalogSource(readText(catalogSourcePath));
  REQUIRE(source.isOk());
  request.catalogSource = &source.value();
  request.fragmentPaths = fragmentPaths;
  request.outPath = (parent / "cross_scene.catalog.txt").string();
  request.closures = {{sceneGuids[0], (parent / "cross_scene.a_closure.catalog.txt").string()}};
  const auto assembled = as::assembleAssetCatalog(request);
  INFO((assembled.isErr() ? std::string(as::toString(assembled.error().error)) + ": " + assembled.error().subject
                          : std::string("ok")));
  REQUIRE(assembled.isOk());
  CHECK(assembled.value().recordCount == 5 + 2);  // the import's five records plus the two scenes
  {
    std::ofstream(request.outPath, std::ios::binary) << assembled.value().catalogText;
  }

  const auto loaded = as::loadAssetCatalog(request.outPath);
  REQUIRE(loaded.isOk());
  const as::AssetCatalog& catalog = loaded.value();
  const as::AssetCatalogRecord* mesh = catalog.find(meshGuid);
  const as::AssetCatalogRecord* material = catalog.find(materialGuid);
  REQUIRE(mesh != nullptr);
  REQUIRE(material != nullptr);
  CHECK(mesh->type == as::CatalogAssetType::Mesh);
  CHECK(material->type == as::CatalogAssetType::Material);
  CHECK(mesh == catalog.find(mesh->assetId));  // one record, reachable by key and by GUID

  std::vector<as::AssetGuid> expectedDependencies = {meshGuid, materialGuid};
  std::sort(expectedDependencies.begin(), expectedDependencies.end());
  for (const as::AssetGuid& sceneGuid : sceneGuids) {
    const as::AssetCatalogRecord* scene = catalog.find(sceneGuid);
    REQUIRE(scene != nullptr);
    CHECK(scene->type == as::CatalogAssetType::Scene);
    CHECK(scene->source.root == as::CatalogRoot::Assets);
    CHECK(scene->dependencies == expectedDependencies);
    for (const as::AssetGuid& dependency : scene->dependencies) {
      // Both scenes' references land on the import's own records.
      CHECK(catalog.find(dependency) == (dependency == meshGuid ? mesh : material));
    }
  }

  // The closure of scene a spans both roots: the scene, the import's mesh and
  // material, and the material's two textures.
  REQUIRE(assembled.value().closureTexts.size() == 1);
  const auto closure = as::parseAssetCatalogRecords(assembled.value().closureTexts[0]);
  REQUIRE(closure.isOk());
  CHECK(closure.value().size() == 5);
}
