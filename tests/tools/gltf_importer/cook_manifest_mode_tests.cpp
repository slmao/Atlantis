#include "gltf_test_builder.h"
#include "import_command.h"
#include "material_import.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

as::AssetId idOf(const std::string& logicalPath) { return as::computeAssetId(as::normalizeLogicalPath(logicalPath).value()); }

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
  const auto result = atlantis::gltf_importer::importGltf(input, fixture.dir, fixture.importDir, "q");
  REQUIRE(result.isOk());
  return fixture;
}

std::vector<std::string> cookManifestArguments(const ImportedFixture& fixture, const fs::path& manifestOut) {
  return {"--kind=cook-manifest", "--import-dir=" + fixture.importDir.generic_string(),
          "--cooked-dir=" + fixture.cookedDir.generic_string(),
          "--content-parent=" + fixture.dir.parent_path().generic_string(),
          "--manifest-out=" + manifestOut.generic_string(),
          "--stamp=" + (fixture.cookedDir / "q.stamp").generic_string()};
}

}  // namespace

TEST_CASE("cook-manifest mode cooks every manifest line in one process and writes a dependency manifest listing "
          "each declared asset once, at its artifact",
          "[asset_cooker][cook_manifest]") {
  const ImportedFixture fixture = importFixture("cook_manifest_mode");
  const fs::path manifestOut = fixture.cookedDir / "q.ascene.manifest.txt";
  REQUIRE(runCooker(cookManifestArguments(fixture, manifestOut)) == 0);
  CHECK(fs::exists(fixture.cookedDir / "q.stamp"));

  // The scene is cooked (no AssetId, so no manifest entry).
  const auto scene = as::decodeSceneArtifact(readBytes(fixture.cookedDir / "q/q.ascene"));
  CHECK(scene.isOk());

  const std::vector<std::string> declared = lines(readText(fixture.importDir / "asset_list.txt"));
  const std::vector<std::string> entries = lines(readText(manifestOut));
  REQUIRE(declared.size() == 4);  // 1 mesh, 1 material, 2 textures
  REQUIRE(entries.size() == declared.size());
  std::set<std::string> seen;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    INFO(entries[i]);
    const std::size_t tab1 = entries[i].find('\t');
    const std::size_t tab2 = entries[i].find('\t', tab1 + 1);
    REQUIRE(tab2 != std::string::npos);
    const std::string logical = entries[i].substr(0, tab1);
    const fs::path artifact = entries[i].substr(tab1 + 1, tab2 - tab1 - 1);
    const fs::path metadata = entries[i].substr(tab2 + 1);
    CHECK(logical == declared[i]);  // the asset list's own order
    CHECK(seen.insert(logical).second);
    CHECK(fs::exists(artifact));
    CHECK(fs::exists(metadata));
    // Meshes stay where the importer wrote them; everything else is cooked.
    const fs::path expectedRoot = logical.rfind("meshes/", 0) == 0 ? fixture.importDir : fixture.cookedDir;
    CHECK(artifact.generic_string().rfind(expectedRoot.generic_string(), 0) == 0);
  }

  // The material names both textures, and both resolve through the manifest.
  const auto material = as::decodeMaterialArtifact(readBytes(fixture.cookedDir / "q/materials/0.amaterial"));
  REQUIRE(material.isOk());
  std::set<as::AssetId> listedIds;
  for (const std::string& entry : entries) listedIds.insert(idOf(entry.substr(0, entry.find('\t'))));
  CHECK(listedIds.count(material.value().textureAsset) == 1);
  CHECK(material.value().emissiveTexture != 0);
  CHECK(listedIds.count(material.value().emissiveTexture) == 1);

  // Spec 0046 Q8: both colour-used DXGI 99 textures cooked as sRGB.
  std::size_t srgbTextures = 0;
  for (const std::string& entry : entries) {
    const std::size_t tab1 = entry.find('\t');
    const std::string artifact = entry.substr(tab1 + 1, entry.find('\t', tab1 + 1) - tab1 - 1);
    if (!artifact.ends_with(".atex")) continue;
    const auto texture = as::decodeTextureArtifact(readBytes(artifact));
    REQUIRE(texture.isOk());
    srgbTextures += texture.value().colorSpace == as::TextureColorSpace::Srgb ? 1 : 0;
  }
  CHECK(srgbTextures == 2);
}

TEST_CASE("cook-manifest mode fails the whole step on one failing line, and on a missing required flag",
          "[asset_cooker][cook_manifest]") {
  const ImportedFixture fixture = importFixture("cook_manifest_mode_failure");
  const fs::path manifestOut = fixture.cookedDir / "q.ascene.manifest.txt";

  // A texture source that no longer exists fails its line, so the mode.
  fs::remove(fixture.dir / "quad_em.dds");
  CHECK(runCooker(cookManifestArguments(fixture, manifestOut)) != 0);
  CHECK_FALSE(fs::exists(manifestOut));
  CHECK_FALSE(fs::exists(fixture.cookedDir / "q.stamp"));

  // The mode's own inputs are all required.
  std::vector<std::string> missingContentParent = cookManifestArguments(fixture, manifestOut);
  missingContentParent.erase(missingContentParent.begin() + 3);
  CHECK(runCooker(missingContentParent) != 0);
}
