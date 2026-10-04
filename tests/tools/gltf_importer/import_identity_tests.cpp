// Plan 0047 M3 step 4 (P5, P7, ruling I2; ADR-0097 D4): the importer takes
// its root GUID from the catalog source and derives every sub-asset's GUID,
// and every imported node's EntityGuid, from it. Outputs carry them: mesh
// sidecars, material v10 and scene v7 text, --guid= on cook-manifest lines.

#include "gltf_test_builder.h"
#include "import_command.h"
#include "material_import.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/material_source.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/scene_source.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
using atlantis::gltf_importer::GltfImportError;
using atlantis::gltf_importer::importGltf;
using atlantis::gltf_importer::resolveImportRoot;

namespace {

constexpr const char* kRootText = "0047ffff-0000-4000-8000-000000000001";

[[nodiscard]] as::AssetGuid root() { return as::parseAssetGuid(kRootText).value(); }

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A quad with one material naming a DDS base colour, under a content root
// named `rootName`.
[[nodiscard]] fs::path writeTexturedQuad(const fs::path& contentRoot) {
  fs::create_directories(contentRoot);
  const std::vector<unsigned char> dds = atlantis::gltf_importer::detail::whiteFallbackDds();
  std::ofstream(contentRoot / "quad_diff.dds", std::ios::binary)
      .write(reinterpret_cast<const char*>(dds.data()), static_cast<std::streamsize>(dds.size()));
  auto spec = gltf_test::unitQuad();
  spec.imagesJson = "[{\"uri\":\"missing.png\"},{\"uri\":\"quad_diff.dds\"}]";
  spec.texturesJson = "[{\"source\":0,\"extensions\":{\"MSFT_texture_dds\":{\"source\":1}}}]";
  spec.extensionsUsedJson = "[\"MSFT_texture_dds\"]";
  spec.materialsJson = "[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}]";
  return gltf_test::writeGltf(contentRoot, spec);
}

void writeCatalog(const fs::path& path, const std::string& entries, std::size_t count) {
  std::ofstream(path, std::ios::binary) << "atlantis_asset_catalog_source_version: 1\nentry_count: " +
                                               std::to_string(count) + "\n" + entries;
}

}  // namespace

TEST_CASE("resolveImportRoot finds the glTF's gltf_import entry by content root and file name",
          "[gltf_importer][guid]") {
  const fs::path dir = gltf_test::freshDirectory("import_identity_resolve");
  const fs::path content = dir / "street";
  const fs::path input = writeTexturedQuad(content);
  const fs::path catalog = dir / "asset_catalog.txt";

  writeCatalog(catalog, std::string("asset: guid=") + kRootText + " type=gltf_import root=content path=street/input.gltf\n",
               1);
  REQUIRE(resolveImportRoot(catalog, input, content).isOk());
  CHECK(resolveImportRoot(catalog, input, content).value() == root());
  CHECK(resolveImportRoot(catalog, input, content.string() + "/").value() == root());  // trailing separator

  writeCatalog(catalog, "", 0);
  CHECK(resolveImportRoot(catalog, input, content).error() == GltfImportError::SourceNotInCatalog);

  writeCatalog(catalog, std::string("asset: guid=") + kRootText + " type=scene root=content path=street/input.gltf\n", 1);
  CHECK(resolveImportRoot(catalog, input, content).error() == GltfImportError::CatalogTypeMismatch);

  writeCatalog(catalog, std::string("asset: guid=") + kRootText + " type=gltf_import root=assets path=street/input.gltf\n",
               1);
  CHECK(resolveImportRoot(catalog, input, content).error() == GltfImportError::SourceNotInCatalog);

  CHECK(resolveImportRoot(dir / "missing.txt", input, content).error() == GltfImportError::CatalogSourceUnreadable);
  writeCatalog(catalog, "not an entry\n", 1);
  CHECK(resolveImportRoot(catalog, input, content).error() == GltfImportError::CatalogSourceInvalid);
}

TEST_CASE("Every import output carries the GUIDs derived from the import root", "[gltf_importer][guid]") {
  const fs::path dir = gltf_test::freshDirectory("import_identity_outputs");
  const fs::path content = dir / "street";
  const fs::path input = writeTexturedQuad(content);
  const fs::path out = dir / "out";
  REQUIRE(importGltf(input, content, out, "t", root()).isOk());

  const auto mesh = as::parseAssetMetadata(readText(out / "t_mesh_0_0.amesh.meta.txt"));
  REQUIRE(mesh.isOk());
  CHECK(mesh.value().assetGuid == as::deriveAssetGuid(root(), "mesh/0/0"));
  CHECK(mesh.value().assetId == as::assetKey(mesh.value().assetGuid));
  CHECK(mesh.value().importerVersion == "atlantis-gltf-importer/1");

  const auto material = as::parseMaterialSource(readText(out / "t/materials/0.material.txt"));
  REQUIRE(material.isOk());
  CHECK(material.value().textureAsset == as::deriveAssetGuid(root(), "texture/quad_diff.dds"));

  const as::AssetGuid sceneGuid = as::deriveAssetGuid(root(), "scene");
  const auto scene = as::parseSceneSource(readText(out / "t/t.scene.txt"));
  REQUIRE(scene.isOk());
  REQUIRE(scene.value().nodes.size() == 1);
  CHECK(scene.value().nodes[0].entityGuid == as::deriveEntityGuid(sceneGuid, "node/0"));
  CHECK(scene.value().nodes[0].meshAsset == as::deriveAssetGuid(root(), "mesh/0/0"));
  CHECK(scene.value().nodes[0].materialAsset == as::deriveAssetGuid(root(), "material/0"));

  const std::string manifest = readText(out / "cook_manifest.txt");
  CHECK(manifest.find("--guid=" + as::toString(as::deriveAssetGuid(root(), "texture/quad_diff.dds"))) !=
        std::string::npos);
  CHECK(manifest.find("--guid=" + as::toString(as::deriveAssetGuid(root(), "material/0"))) != std::string::npos);
  CHECK(manifest.find("--guid=" + as::toString(sceneGuid)) != std::string::npos);
}

TEST_CASE("The importer writes each mesh's catalog fragment and names every cook line's source",
          "[gltf_importer][guid][catalog]") {
  const fs::path dir = gltf_test::freshDirectory("import_identity_fragments");
  const fs::path content = dir / "street";
  const fs::path input = writeTexturedQuad(content);
  const fs::path out = dir / "out";
  REQUIRE(importGltf(input, content, out, "t", root()).isOk());

  // Plan 0047 P12: located through {import_dir}, so the import output does
  // not depend on where it was written.
  std::string fragment = readText(out / "t_mesh_0_0.amesh.catalog.txt");
  CHECK(fragment.find(" artifact={import_dir}/t_mesh_0_0.amesh ") != std::string::npos);
  CHECK(fragment.find(" metadata={import_dir}/t_mesh_0_0.amesh.meta.txt ") != std::string::npos);
  const auto records = as::parseAssetCatalogRecords(fragment);
  REQUIRE(records.isOk());
  REQUIRE(records.value().size() == 1);
  const as::AssetCatalogRecord& mesh = records.value()[0];
  CHECK(mesh.guid == as::deriveAssetGuid(root(), "mesh/0/0"));
  CHECK(mesh.assetId == as::assetKey(mesh.guid));
  CHECK(mesh.type == as::CatalogAssetType::Mesh);
  CHECK(as::toString(mesh.source) == "content:street/input.gltf#mesh/0/0");
  CHECK(mesh.artifactSchema == as::kMeshArtifactSchemaVersionU32);
  CHECK_FALSE(mesh.sourceSchema.has_value());
  CHECK(mesh.tool == "atlantis-gltf-importer/1");
  CHECK(mesh.dependencies.empty());

  const std::string manifest = readText(out / "cook_manifest.txt");
  std::size_t cookLines = 0;
  std::istringstream lines(manifest);
  for (std::string line; std::getline(lines, line);) {
    if (line.empty() || line[0] == '#') continue;
    ++cookLines;
    INFO(line);
    CHECK(line.find(" --catalog-id=content:street/input.gltf#") != std::string::npos);
  }
  CHECK(cookLines == 3);
  for (const char* id : {"#texture/quad_diff.dds", "#material/0", "#scene"}) {
    CHECK(manifest.find(std::string("--catalog-id=content:street/input.gltf") + id) != std::string::npos);
  }
}

TEST_CASE("Imported GUIDs do not depend on the content root's name or location", "[gltf_importer][guid]") {
  const fs::path dir = gltf_test::freshDirectory("import_identity_moved");
  const fs::path inputA = writeTexturedQuad(dir / "street");
  const fs::path inputB = writeTexturedQuad(dir / "elsewhere" / "renamed_street");
  REQUIRE(importGltf(inputA, dir / "street", dir / "out_a", "t", root()).isOk());
  REQUIRE(importGltf(inputB, dir / "elsewhere" / "renamed_street", dir / "out_b", "t", root()).isOk());

  for (const char* file : {"t_mesh_0_0.amesh", "t_mesh_0_0.amesh.meta.txt", "t/t.scene.txt"}) {
    INFO(file);
    CHECK(readText(dir / "out_a" / file) == readText(dir / "out_b" / file));
  }
  const auto a = as::parseMaterialSource(readText(dir / "out_a" / "t/materials/0.material.txt"));
  const auto b = as::parseMaterialSource(readText(dir / "out_b" / "t/materials/0.material.txt"));
  REQUIRE(a.isOk());
  REQUIRE(b.isOk());
  CHECK(a.value().textureAsset == b.value().textureAsset);
}

// Plan 0047 M9 (R14): an import from a moved glTF and a renamed content root
// yields the same GUIDs, because the root's identity lives in the catalog
// source and moving the content costs exactly that one catalog line.
TEST_CASE("Importing a glTF from a moved file and content root, with only its catalog line edited, yields the "
          "same GUIDs and outputs",
          "[gltf_importer][guid][stable_identity]") {
  const fs::path dir = gltf_test::freshDirectory("import_identity_r14");
  const fs::path contentA = dir / "street";
  const fs::path inputA = writeTexturedQuad(contentA);
  const fs::path catalogA = dir / "catalog_a.txt";
  writeCatalog(catalogA,
               std::string("asset: guid=") + kRootText + " type=gltf_import root=content path=street/input.gltf\n", 1);

  const auto rootA = resolveImportRoot(catalogA, inputA, contentA);
  REQUIRE(rootA.isOk());
  REQUIRE(importGltf(inputA, contentA, dir / "out_a", "t", rootA.value()).isOk());

  // Move: the content root is renamed and relocated, and the glTF renamed.
  const fs::path contentB = dir / "elsewhere" / "renamed_street";
  fs::create_directories(contentB.parent_path());
  fs::rename(contentA, contentB);
  fs::rename(contentB / "input.gltf", contentB / "renamed.gltf");
  const fs::path inputB = contentB / "renamed.gltf";

  // Without the catalog edit the moved import is not found.
  const auto stale = resolveImportRoot(catalogA, inputB, contentB);
  REQUIRE(stale.isErr());
  CHECK(stale.error() == GltfImportError::SourceNotInCatalog);

  // With its one line edited, the same GUID resolves and the import is the same.
  const fs::path catalogB = dir / "catalog_b.txt";
  writeCatalog(catalogB,
               std::string("asset: guid=") + kRootText +
                   " type=gltf_import root=content path=renamed_street/renamed.gltf\n",
               1);
  const auto rootB = resolveImportRoot(catalogB, inputB, contentB);
  REQUIRE(rootB.isOk());
  CHECK(rootB.value() == rootA.value());
  REQUIRE(importGltf(inputB, contentB, dir / "out_b", "t", rootB.value()).isOk());

  for (const char* file : {"t_mesh_0_0.amesh", "t_mesh_0_0.amesh.meta.txt", "t/t.scene.txt", "t/materials/0.material.txt"}) {
    INFO(file);
    CHECK(readText(dir / "out_a" / file) == readText(dir / "out_b" / file));
  }
  const auto metadataA = as::parseAssetMetadata(readText(dir / "out_a" / "t_mesh_0_0.amesh.meta.txt"));
  const auto metadataB = as::parseAssetMetadata(readText(dir / "out_b" / "t_mesh_0_0.amesh.meta.txt"));
  REQUIRE(metadataA.isOk());
  REQUIRE(metadataB.isOk());
  CHECK(metadataA.value().assetGuid == metadataB.value().assetGuid);
  CHECK(metadataA.value().assetGuid == as::deriveAssetGuid(root(), "mesh/0/0"));
}

TEST_CASE("Import sub-keys must be non-empty ASCII (ruling I2)", "[gltf_importer][guid]") {
  using atlantis::gltf_importer::detail::deriveImportAssetGuid;
  using atlantis::gltf_importer::detail::deriveImportEntityGuid;
  CHECK(deriveImportAssetGuid(root(), "mesh/0/0").value() == as::deriveAssetGuid(root(), "mesh/0/0"));
  CHECK(deriveImportEntityGuid(root(), "node/3").value() == as::deriveEntityGuid(root(), "node/3"));
  CHECK(deriveImportAssetGuid(root(), "").error() == GltfImportError::InvalidAssetSubKey);
  CHECK(deriveImportAssetGuid(root(), "texture/caf\xC3\xA9.dds").error() == GltfImportError::InvalidAssetSubKey);
  CHECK(deriveImportEntityGuid(root(), "").error() == GltfImportError::InvalidAssetSubKey);
}

TEST_CASE("The real atlantis_gltf_importer produces byte-identical output across two runs",
          "[gltf_importer][guid][tool]") {
  const fs::path dir = gltf_test::freshDirectory("import_identity_determinism");
  const fs::path content = dir / "street";
  const fs::path input = writeTexturedQuad(content);
  const fs::path catalog = dir / "asset_catalog.txt";
  writeCatalog(catalog, std::string("asset: guid=") + kRootText + " type=gltf_import root=content path=street/input.gltf\n",
               1);

  const auto run = [&](const fs::path& out) {
    const std::string command = "\"\"" + std::string(ATLANTIS_GLTF_IMPORTER_EXECUTABLE) + "\" --input=\"" +
                                input.string() + "\" --content-root=\"" + content.string() + "\" --output-dir=\"" +
                                out.string() + "\" --name=t --catalog-source=\"" + catalog.string() + "\"\"";
    return std::system(command.c_str());
  };
  REQUIRE(run(dir / "out_a") == 0);
  REQUIRE(run(dir / "out_b") == 0);

  std::vector<std::string> files;
  for (const auto& entry : fs::recursive_directory_iterator(dir / "out_a")) {
    if (entry.is_regular_file()) files.push_back(fs::relative(entry.path(), dir / "out_a").generic_string());
  }
  REQUIRE_FALSE(files.empty());
  for (const std::string& file : files) {
    INFO(file);
    REQUIRE(fs::exists(dir / "out_b" / file));
    CHECK(readText(dir / "out_a" / file) == readText(dir / "out_b" / file));
  }
}
