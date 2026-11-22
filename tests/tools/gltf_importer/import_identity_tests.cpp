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

#include <algorithm>
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

  // The configure-time check sees the actual output GUIDs even when the
  // texture payload is unavailable and the importer cannot cook this scene.
  REQUIRE(fs::remove(content / "quad_diff.dds"));
  const auto listed = atlantis::gltf_importer::importAssetGuids(input, root());
  REQUIRE(listed.isOk());
  const std::vector expected{mesh.value().assetGuid, as::deriveAssetGuid(root(), "material/0"),
                             material.value().textureAsset, sceneGuid};
  CHECK(std::ranges::is_permutation(listed.value(), expected));
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

TEST_CASE("managed glTF imports only a bounded self-contained static mesh", "[gltf_importer][guid]") {
  const auto dir = gltf_test::freshDirectory("managed_static_mesh");
  const auto input = dir / "source.gltf";
  const auto out = dir / "output";
  auto text = gltf_test::buildGltf(gltf_test::unitQuad());
  bool accepted = false;
  auto expectedError = GltfImportError::MalformedGltf;
  SECTION("embedded indexed mesh") { accepted = true; }
  SECTION("legacy single mesh retains recognized spec-gloss conversion") {
    auto geometry = gltf_test::unitQuad();
    geometry.materialsJson = R"([{"extensions":{"KHR_materials_pbrSpecularGlossiness":{"glossinessFactor":0.5}}}])";
    text = gltf_test::buildGltf(geometry);
    accepted = true;
  }
  SECTION("external buffer is refused before loading") {
    const auto uri = text.find("data:application/octet-stream;base64,");
    REQUIRE(uri != std::string::npos);
    text.replace(uri, text.find('"', uri) - uri, "missing.bin");
  }
  SECTION("external image is refused before loading") {
    text.insert(text.find('{') + 1, "\"images\":[{\"uri\":\"missing.png\"}],");
  }
  SECTION("invalid embedded base64 is refused") {
    constexpr std::string_view prefix = "data:application/octet-stream;base64,";
    const auto uri = text.find(prefix);
    REQUIRE(uri != std::string::npos);
    text[uri + prefix.size()] = '?';
  }
  SECTION("declared buffer size must match embedded bytes") {
    const auto length = text.rfind("\"byteLength\":");
    REQUIRE(length != std::string::npos);
    const auto start = length + std::string_view("\"byteLength\":").size();
    text.replace(start, text.find(',', start) - start, "400000000");
  }
  SECTION("non-triangle geometry is refused") {
    auto geometry = gltf_test::unitQuad();
    geometry.mode = 1;
    text = gltf_test::buildGltf(geometry);
    expectedError = GltfImportError::NonTrianglesMode;
  }
  SECTION("oversized source is refused") { text.resize(16 * 1024 * 1024 + 1, ' '); }
  std::ofstream(input, std::ios::binary) << text;
  const auto result = importGltf(input, dir, out, "managed_mesh", root(), std::nullopt, true);
  if (accepted) {
    REQUIRE(result.isOk());
    CHECK(result.value().meshCount == 1);
  } else {
    REQUIRE(result.isErr());
    CHECK(result.error() == expectedError);
    CHECK_FALSE(fs::exists(out));
  }
}

TEST_CASE("managed static group counts emitted instances and retains local hierarchy", "[gltf_importer][guid][static-group]") {
  const auto dir = gltf_test::freshDirectory("managed_static_group");
  auto geometry = gltf_test::unitQuad();
  geometry.nodesJson = R"json([{"mesh":0,"children":[1],"translation":[1,2,3]},{"mesh":0,"scale":[2,3,4]},{"mesh":0}])json";
  geometry.sceneRootsJson = "[0,2]";
  bool accepted = false;
  bool alternateShape = false;
  bool addPrimitive = false;
  SECTION("multiple roots, nested transform and shared mesh") { accepted = true; }
  SECTION("unreferenced declared node") { geometry.sceneRootsJson = "[0]"; }
  SECTION("zero scale") {
    geometry.nodesJson = R"json([{"mesh":0},{"mesh":0,"scale":[0,1,1]}])json";
    geometry.sceneRootsJson = "[0,1]";
  }
  SECTION("double-sided material") { geometry.materialsJson = R"([{"doubleSided":true}])"; }
  SECTION("recognized material extension without extensionsUsed") {
    geometry.materialsJson = R"([{"extensions":{"KHR_materials_pbrSpecularGlossiness":{"glossinessFactor":0.5}}}])";
  }
  SECTION("explicit opaque PBR material") {
    geometry.materialsJson = R"([{"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.7,1],"metallicFactor":0.6,"roughnessFactor":0.8}}])";
    accepted = true;
  }
  SECTION("63 emitted nodes including two synthetic children") {
    alternateShape = true; addPrimitive = true; accepted = true;
  }
  SECTION("64 emitted nodes including two synthetic children") {
    alternateShape = true; addPrimitive = true;
  }
  if (alternateShape) {
    geometry.nodesJson = R"([{"mesh":0})";
    geometry.sceneRootsJson = "[0";
    for (int i = 1; i < (accepted ? 61 : 62); ++i) {
      geometry.nodesJson += ",{}";
      geometry.sceneRootsJson += "," + std::to_string(i);
    }
    geometry.nodesJson += "]";
    geometry.sceneRootsJson += "]";
  }
  auto source = gltf_test::buildGltf(geometry);
  if (addPrimitive) {
    const auto first = source.find("{\"attributes\"");
    const auto last = source.find("}]}]", first);
    REQUIRE(first != std::string::npos);
    REQUIRE(last != std::string::npos);
    auto second = source.substr(first, last - first + 1);
    source.insert(last + 1, "," + second);
  }
  const auto input = dir / "source.gltf";
  const auto out = dir / "output";
  std::ofstream(input, std::ios::binary) << source;
  const auto result = importGltf(input, dir, out, "storefront", root(), std::nullopt, true);
  if (!accepted) {
    REQUIRE(result.isErr());
    CHECK_FALSE(fs::exists(out));
    return;
  }
  REQUIRE(result.isOk());
  CHECK(result.value().meshCount == (addPrimitive ? 2u : 1u));
  const auto parsed = as::parseSceneSource(readText(out / "storefront/storefront.scene.txt"));
  REQUIRE(parsed.isOk());
  REQUIRE(parsed.value().nodes.size() == (alternateShape ? 63u : 3u));
  if (!alternateShape) {
    const auto& nodes = parsed.value().nodes;
    CHECK(nodes[1].parentNodeId == nodes[0].nodeId);
    CHECK(nodes[1].transform.scaleY == 3.f);
    CHECK(nodes[2].meshAsset == nodes[0].meshAsset);
    CHECK(nodes[1].materialAsset.has_value() == !geometry.materialsJson.empty());
  }
}

TEST_CASE("Managed keyed glTF derives identity from explicit keys and refuses duplicate primitive keys",
          "[gltf_importer][guid][managed]") {
  const fs::path dir = gltf_test::freshDirectory("keyed_import_identity");
  auto spec = gltf_test::unitQuad();
  spec.materialsJson = R"([{"extras":{"atlantisId":"stone"}},{"extras":{"atlantisId":"trim"}}])";
  std::string gltf = gltf_test::buildGltf(spec);
  auto insert = [&](std::string_view from, std::string_view to) {
    const auto position = gltf.find(from);
    REQUIRE(position != std::string::npos);
    gltf.replace(position, from.size(), to);
  };
  insert(R"("version":"2.0")", R"("version":"2.0","extras":{"atlantisReimportIdentity":1})");
  insert(R"("meshes":[{"primitives":[{)", R"("meshes":[{"extras":{"atlantisId":"wall"},"primitives":[{"extras":{"atlantisId":"face"},)");
  const auto begin = gltf.find(R"("primitives":[)") + std::string_view(R"("primitives":[)").size();
  const auto end = gltf.find("]}]", begin);
  REQUIRE(end != std::string::npos);
  auto trim = gltf.substr(begin, end - begin);
  trim.replace(trim.find("face"), 4, "trim");
  trim.replace(trim.find(R"("material":0)"), 12, R"("material":1)");
  gltf.insert(end, "," + trim);
  const auto first = dir / "first.gltf";
  const auto next = dir / "next.gltf";
  std::ofstream(first, std::ios::binary) << gltf;
  REQUIRE(importGltf(first, dir, dir / "first", "keyed", root(), std::nullopt, true, true).isOk());
  insert(R"("material":1)", R"("material":2)");
  insert(R"("material":0)", R"("material":1)");
  insert(R"("material":2)", R"("material":0)");
  insert(R"("atlantisId":"stone"}},{"extras":{"atlantisId":"trim")", R"("atlantisId":"trim"}},{"extras":{"atlantisId":"stone")");
  std::ofstream(next, std::ios::binary) << gltf;
  REQUIRE(importGltf(next, dir, dir / "next", "keyed", root(), std::nullopt, true, true).isOk());
  const auto mesh = fs::path("keyed_mesh-key.wall.face.amesh.meta.txt");
  CHECK(readText(dir / "first" / mesh) == readText(dir / "next" / mesh));
  const auto metadata = as::parseAssetMetadata(readText(dir / "next" / mesh));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().assetGuid == as::deriveAssetGuid(root(), "mesh-key/wall/face"));
  const auto scene = as::parseSceneSource(readText(dir / "next/keyed/keyed.scene.txt"));
  REQUIRE(scene.isOk());
  const auto face = std::ranges::find(scene.value().nodes, metadata.value().assetGuid, &as::ParsedSceneNode::meshAsset);
  REQUIRE(face != scene.value().nodes.end());
  CHECK(face->materialAsset == as::deriveAssetGuid(root(), "material-key/stone"));

  insert(R"("atlantisId":"trim"},"attributes")", R"("atlantisId":"face"},"attributes")");
  std::ofstream(next, std::ios::binary) << gltf;
  CHECK(importGltf(next, dir, dir / "invalid", "keyed", root(), std::nullopt, true, true).isErr());
}
