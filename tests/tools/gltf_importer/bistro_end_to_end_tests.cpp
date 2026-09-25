#include "import_command.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/material_source.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Plan 0037 Milestone 6: the real Bistro chain end to end on a small sample
// -- importer -> real atlantis_asset_cooker (a subprocess, the same binary a
// human runs) -> --validate-set -> the public artifact decoders. The census
// test and a one-off full sweep (reported in the PR) cover the full counts;
// this standing test keeps the whole chain honest without re-asserting them.
//
// Deliberately NOT exercised: loading the imported scene through Runtime.
// This test's validation stops at the Asset System decoders because that
// is its own scope, not because the meshes cannot be drawn -- Spec 0039
// (Spec 0036 workflow 1b) parameterized the index type, so imported
// .amesh schema-5 meshes now load through loadStaticMeshAsset() and
// render, which tests/image_regression/bistro_large_mesh_gpu_tests.cpp
// proves on the largest of them. Plan 0037 Ruling 5's disclosure is
// therefore closed; drawing the whole imported *scene* remains Spec 0036
// workflow 7's job.

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;

namespace {

std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::byte> readBytes(const fs::path& path) {
  const std::string text = readText(path);
  std::vector<std::byte> bytes(text.size());
  if (!text.empty()) std::memcpy(bytes.data(), text.data(), text.size());
  return bytes;
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
  }
  return out;
}

std::string valueOf(const std::string& manifestLine, const std::string& key) {
  const std::size_t start = manifestLine.find(key);
  if (start == std::string::npos) return {};
  const std::size_t end = manifestLine.find(' ', start);
  return manifestLine.substr(start + key.size(), end == std::string::npos ? std::string::npos : end - start - key.size());
}

// Runs the real cooker on one manifest line. Each --key=value argument's
// value is quoted, and the whole command gets one extra enclosing pair of
// quotes for cmd.exe (the same std::system() rule
// tests/tools/asset_cooker/cooker_determinism_tests.cpp documents).
int runCooker(const std::string& arguments) {
  std::string command = "\"" + std::string(ATLANTIS_ASSET_COOKER_EXECUTABLE) + "\"";
  std::istringstream tokens(arguments);
  std::string token;
  while (tokens >> token) {
    const std::size_t eq = token.find('=');
    command += " " + (eq == std::string::npos ? token : token.substr(0, eq + 1) + "\"" + token.substr(eq + 1) + "\"");
  }
  return std::system(("\"" + command + "\"").c_str());
}

as::AssetId idOf(const std::string& logicalPath) {
  return as::computeAssetId(as::normalizeLogicalPath(logicalPath).value());
}

}  // namespace

TEST_CASE("Real Bistro imports, validates and cooks end to end through the real asset cooker", "[bistro]") {
  const fs::path content{ATLANTIS_BISTRO_CONTENT_DIR};
  if (!fs::exists(content / "bistro.gltf")) {
    SKIP("Bistro content not found at " << content.string() << " -- run tools/content/fetch_bistro.ps1");
  }

  const fs::path work = fs::temp_directory_path() / "atlantis_gltf_importer_tests" / "bistro_end_to_end";
  fs::remove_all(work);
  fs::create_directories(work);
  const fs::path importDir = work / "import";
  const fs::path cookedDir = work / "cooked";

  // 1. Import (meshes are written as artifacts directly, ADR-0083 D1).
  const auto imported = atlantis::gltf_importer::importGltf(content / "bistro.gltf", content, importDir, "bistro");
  REQUIRE(imported.isOk());
  CHECK(imported.value().meshCount == 551);
  CHECK(imported.value().materialCount == 254);
  // Plan 0046 Milestone 1 (ADR-0096): 257 + the 8 distinct emissive masks
  // the 11 textured-emissive materials now map.
  CHECK(imported.value().texturesReferenced == 265);
  CHECK(imported.value().sceneNodeLines == 5908);
  CHECK(imported.value().declaredAssets == 551 + 254 + 265);

  // 2. The whole declared asset set passes the cooker's own set validation
  //    (AssetId collisions, case-only path conflicts).
  REQUIRE(runCooker("--validate-set --asset-list=" + (importDir / "asset_list.txt").generic_string()) == 0);

  std::set<as::AssetId> meshIds;
  std::set<as::AssetId> materialIds;
  for (const std::string& path : lines(readText(importDir / "asset_list.txt"))) {
    if (path.rfind("meshes/", 0) == 0) meshIds.insert(idOf(path));
    if (path.find(".material.txt") != std::string::npos) materialIds.insert(idOf(path));
  }

  // 3. The whole set -- every texture, material and the scene -- cooked in
  //    one process by the cooker's cook-manifest mode (Plan 0046 Milestone 2,
  //    ADR-0094 Decision 2), which also writes the dependency manifest; the
  //    wall-clock time is the build-time measurement Plan 0046's M2 gate
  //    reports. Then samples are decoded: material 0 (Ruling 2 fallback with
  //    a normal map), the first material using the white fallback texture
  //    (Ruling 7), their textures, and the scene.
  const fs::path dependencyManifest = cookedDir / "bistro.ascene.manifest.txt";
  const auto cookStart = std::chrono::steady_clock::now();
  REQUIRE(runCooker("--kind=cook-manifest --import-dir=" + importDir.generic_string() +
                    " --cooked-dir=" + cookedDir.generic_string() +
                    " --content-parent=" + content.parent_path().generic_string() +
                    " --manifest-out=" + dependencyManifest.generic_string()) == 0);
  const double cookSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - cookStart).count();
  WARN("full Bistro cook through --kind=cook-manifest: " << cookSeconds << " s");
  {
    std::size_t entries = 0;
    std::set<std::string> logicalPaths;
    for (const std::string& line : lines(readText(dependencyManifest))) {
      if (line.empty()) continue;
      ++entries;
      logicalPaths.insert(line.substr(0, line.find('\t')));
    }
    CHECK(entries == 551 + 254 + 265);
    CHECK(logicalPaths.size() == entries);  // each declared asset exactly once
  }
  std::vector<std::size_t> sampleMaterials = {0};
  for (std::size_t i = 1; i < 254 && sampleMaterials.size() < 2; ++i) {
    const auto source = as::parseMaterialSource(readText(importDir / ("bistro/materials/" + std::to_string(i) + ".material.txt")));
    REQUIRE(source.isOk());
    if (source.value().textureLogicalPath.find("_importer/white") != std::string::npos) sampleMaterials.push_back(i);
  }
  REQUIRE(sampleMaterials.size() == 2);

  std::set<std::string> sampleTextures;
  std::vector<as::ParsedMaterialSource> sampleSources;
  for (const std::size_t i : sampleMaterials) {
    const auto source = as::parseMaterialSource(readText(importDir / ("bistro/materials/" + std::to_string(i) + ".material.txt")));
    REQUIRE(source.isOk());
    sampleTextures.insert(source.value().textureLogicalPath);
    if (!source.value().normalMapLogicalPath.empty()) sampleTextures.insert(source.value().normalMapLogicalPath);
    sampleSources.push_back(source.value());
  }

  std::vector<std::string> textureStems;
  for (const std::string& line : lines(readText(importDir / "cook_manifest.txt"))) {
    if (line.rfind("--kind=texture", 0) != 0) continue;
    const std::string source = valueOf(line, "--source=");
    for (const std::string& t : sampleTextures) {
      if (source.size() >= t.size() && source.compare(source.size() - t.size(), t.size(), t) == 0) {
        textureStems.push_back(fs::path(valueOf(line, "--stamp=")).stem().string());
      }
    }
  }

  // 4. Decode. Meshes: a plain one, one with degenerate-basis fallbacks
  //    (#224) and the largest, over the uint16 range (#246).
  for (const char* mesh : {"bistro_mesh_0_0", "bistro_mesh_224_0", "bistro_mesh_246_0"}) {
    INFO(mesh);
    const auto decoded = as::decodeMeshArtifactU32(readBytes(importDir / (std::string(mesh) + ".amesh")));
    const auto metadata = as::parseAssetMetadata(readText(importDir / (std::string(mesh) + ".amesh.meta.txt")));
    REQUIRE(decoded.isOk());
    REQUIRE(metadata.isOk());
    const std::size_t vertices = decoded.value().vertexBytes.size() / 60;
    CHECK(vertices == metadata.value().vertexCount);
    CHECK(decoded.value().indices.size() == metadata.value().indexCount);
    CHECK(meshIds.count(decoded.value().assetId) == 1);
    std::size_t badW = 0;
    for (std::size_t v = 0; v < vertices; ++v) {
      float w = 0.0f;
      std::memcpy(&w, decoded.value().vertexBytes.data() + v * 60 + 56, sizeof w);
      badW += (w != 1.0f && w != -1.0f) ? 1 : 0;
    }
    CHECK(badW == 0);
  }
  CHECK(as::decodeMeshArtifactU32(readBytes(importDir / "bistro_mesh_246_0.amesh")).value().vertexBytes.size() / 60 ==
        127104);

  // Textures: BC7, block-aligned.
  REQUIRE(textureStems.size() == sampleTextures.size());
  for (const std::string& stem : textureStems) {
    INFO(stem);
    const auto decoded = as::decodeTextureArtifact(readBytes(cookedDir / (stem + ".atex")));
    REQUIRE(decoded.isOk());
    CHECK(decoded.value().layout == as::TextureDataLayout::Bc7);
    CHECK(decoded.value().width % 4 == 0);
    CHECK(decoded.value().height % 4 == 0);
    // Spec 0045: every Bistro DDS carries its full chain to 1x1 (the
    // 2026-09-25 census), and the cook passes all of it through. The
    // importer's own white fallback is a single-level 4x4 DDS by
    // construction (material_import.cpp's whiteFallbackDds()).
    const std::uint32_t expectedLevels =
        stem.find("white_4x4_bc7") != std::string::npos
            ? 1U
            : as::fullMipChainLength(decoded.value().width, decoded.value().height);
    CHECK(decoded.value().mipCount == expectedLevels);
  }

  // Materials: the cooked artifact references exactly the textures its
  // source names.
  for (std::size_t k = 0; k < sampleMaterials.size(); ++k) {
    const auto decoded = as::decodeMaterialArtifact(
        readBytes(cookedDir / ("bistro/materials/" + std::to_string(sampleMaterials[k]) + ".amaterial")));
    REQUIRE(decoded.isOk());
    CHECK(decoded.value().kind == as::MaterialKind::PbrDirectLit);
    CHECK(decoded.value().textureAsset == idOf(sampleSources[k].textureLogicalPath));
    if (!sampleSources[k].normalMapLogicalPath.empty()) {
      CHECK(decoded.value().normalMapTexture == idOf(sampleSources[k].normalMapLogicalPath));
    }
  }

  // Scene: every renderable resolves into the declared mesh/material set.
  const fs::path scene = cookedDir / "bistro/bistro.ascene";
  const auto decodedScene = as::decodeScene(scene.string(), scene.string() + ".meta.txt");
  REQUIRE(decodedScene.isOk());
  CHECK(decodedScene.value().nodeCount() == 5908);
  std::size_t renderables = 0;
  std::size_t unresolved = 0;
  for (std::size_t i = 0; i < decodedScene.value().nodeCount(); ++i) {
    const auto& renderable = decodedScene.value().node(i).renderable;
    if (!renderable) continue;
    ++renderables;
    unresolved += meshIds.count(renderable->meshAsset) == 0 ? 1 : 0;
    unresolved += (!renderable->materialAsset || materialIds.count(*renderable->materialAsset) == 0) ? 1 : 0;
  }
  CHECK(renderables == 2909);
  CHECK(unresolved == 0);

  fs::remove_all(work);
}
