#include "gltf_test_builder.h"
#include "import_command.h"
#include "material_conversion.h"
#include "material_import.h"

#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/material_source.h>

#include <dds_parser.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Plan 0037 Milestone 4: the material/texture slice. Fixtures are tiny glTF
// files built in memory; textures they reference are small DDS files each
// test writes next to its .gltf.

namespace fs = std::filesystem;
using atlantis::gltf_importer::GltfImportError;
using atlantis::gltf_importer::GltfImportSummary;
using atlantis::gltf_importer::importGltf;
using atlantis::gltf_importer::convertSpecularGlossiness;
using atlantis::gltf_importer::SpecularGlossinessFactors;

namespace {

struct MaterialRun {
  fs::path dir;
  fs::path outputDir;
  atlantis::Result<GltfImportSummary, GltfImportError> result;
};

// Writes `ddsNames` as (valid 4x4 BC7) DDS files next to the .gltf, then imports.
MaterialRun runMaterialImport(const std::string& testName, gltf_test::PrimitiveSpec spec,
                              const std::vector<std::string>& ddsNames = {}) {
  const fs::path dir = gltf_test::freshDirectory(testName);
  const std::vector<unsigned char> dds = atlantis::gltf_importer::detail::whiteFallbackDds();
  for (const std::string& file : ddsNames) {
    std::ofstream(dir / file, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),
                                                      static_cast<std::streamsize>(dds.size()));
  }
  const fs::path input = gltf_test::writeGltf(dir, spec);
  const fs::path outputDir = dir / "out";
  return MaterialRun{dir, outputDir, importGltf(input, dir, outputDir, "t")};
}

std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

atlantis::asset_system::ParsedMaterialSource parsedMaterial0(const fs::path& outputDir) {
  const auto parsed = atlantis::asset_system::parseMaterialSource(readText(outputDir / "t/materials/0.material.txt"));
  REQUIRE(parsed.isOk());
  return parsed.value();
}

bool reportContains(const GltfImportSummary& summary, const std::string& needle) {
  return std::any_of(summary.reportLines.begin(), summary.reportLines.end(),
                     [&](const std::string& line) { return line.find(needle) != std::string::npos; });
}

// One DDS-backed texture (textures[0]) whose MSFT_texture_dds source is
// images[1] = ddsName; images[0] is the core PNG fallback, which need not exist.
void addDdsTexture(gltf_test::PrimitiveSpec& spec, const std::string& ddsName, const std::string& sampler = "") {
  spec.texturesJson = "[{\"source\":0" + (sampler.empty() ? std::string() : ",\"sampler\":" + sampler) +
                      ",\"extensions\":{\"MSFT_texture_dds\":{\"source\":1}}}]";
  spec.imagesJson = "[{\"uri\":\"missing.png\"},{\"uri\":\"" + ddsName + "\"}]";
  spec.extensionsUsedJson = "[\"MSFT_texture_dds\",\"KHR_materials_pbrSpecularGlossiness\"]";
}

}  // namespace

TEST_CASE("D3 spec-gloss -> metallic-roughness conversion matches the Khronos reference formula",
          "[gltf_importer][material]") {
  // Expected values computed independently (Python) from the cited
  // babylon.pbrUtilities.js ConvertToMetallicRoughness, KhronosGroup/glTF @ 11136cfa.
  struct Case {
    const char* name;
    SpecularGlossinessFactors in;
    std::array<float, 4> baseColor;
    float metallic;
    float roughness;
  };
  const Case cases[] = {
      // Dielectric F0 = 0.04: solve gives 0, base colour is the diffuse colour.
      {"dielectric", {{0.5f, 0.3f, 0.2f, 1.0f}, {0.04f, 0.04f, 0.04f}, 0.6f}, {0.5f, 0.3f, 0.2f, 1.0f}, 0.0f, 0.4f},
      // Gold-like specular, black diffuse: pure metal, base colour = specular.
      {"gold", {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.766f, 0.336f}, 0.9f}, {1.0f, 0.766f, 0.336f, 1.0f}, 1.0f, 0.1f},
      // Partial metal.
      {"mid", {{0.4f, 0.4f, 0.4f, 0.8f}, {0.3f, 0.3f, 0.3f}, 0.25f},
       {0.571243381f, 0.571243381f, 0.571243381f, 0.8f}, 0.489417862f, 0.75f},
      // Perceived specular below 0.04: metallic clamps to 0, diffuse rescaled by (1-maxSpec)/0.96.
      {"below_dielectric", {{0.8f, 0.8f, 0.8f, 1.0f}, {0.02f, 0.02f, 0.02f}, 0.0f},
       {0.816666667f, 0.816666667f, 0.816666667f, 1.0f}, 0.0f, 1.0f},
      // All factors at their defaults (1): a pure white metal -- why Ruling 2
      // keeps textured materials away from this formula.
      {"defaults", {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f},
      {"black_specular", {{0.2f, 0.4f, 0.6f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.5f},
       {0.208333333f, 0.416666667f, 0.625f, 1.0f}, 0.0f, 0.5f},
  };
  for (const Case& c : cases) {
    INFO(c.name);
    const auto out = convertSpecularGlossiness(c.in);
    for (int i = 0; i < 4; ++i) CHECK(out.baseColor[i] == Catch::Approx(c.baseColor[i]).margin(1e-6));
    CHECK(out.metallic == Catch::Approx(c.metallic).margin(1e-6));
    CHECK(out.roughness == Catch::Approx(c.roughness).margin(1e-6));
  }
}

TEST_CASE("A factor-only spec-gloss material converts via D3 and gets the white base-colour fallback",
          "[gltf_importer][material]") {
  auto spec = gltf_test::unitQuad();
  spec.materialsJson =
      "[{\"extensions\":{\"KHR_materials_pbrSpecularGlossiness\":{\"diffuseFactor\":[0.4,0.4,0.4,0.8],"
      "\"specularFactor\":[0.3,0.3,0.3],\"glossinessFactor\":0.25}}}]";
  spec.extensionsUsedJson = "[\"KHR_materials_pbrSpecularGlossiness\"]";
  const MaterialRun run = runMaterialImport("material_formula", spec);
  REQUIRE(run.result.isOk());
  const GltfImportSummary& summary = run.result.value();
  CHECK(summary.materialsSpecGlossFormula == 1);
  CHECK(summary.materialsWhiteFallback == 1);
  CHECK(reportContains(summary, "spec-gloss factors converted (D3"));
  CHECK(reportContains(summary, "white 4x4 BC7 fallback (Ruling 7)"));

  const auto material = parsedMaterial0(run.outputDir);
  CHECK(material.kind == atlantis::asset_system::MaterialKind::PbrDirectLit);
  CHECK(material.metallicFactor == Catch::Approx(0.489417862f).margin(1e-6));
  CHECK(material.roughnessFactor == Catch::Approx(0.75f));
  CHECK(material.baseColorFactor[3] == Catch::Approx(0.8f));
  CHECK(material.textureLogicalPath == "t/_importer/white_4x4_bc7.dds");
  CHECK(material.normalMapLogicalPath.empty());

  // The fallback texture is written, parses as a 4x4 BC7 DDS through the
  // cooker's own parser, and is the one texture line of the manifest.
  const std::vector<std::byte> dds = gltf_test::readBytes(run.outputDir / "t/_importer/white_4x4_bc7.dds");
  const auto parsed = atlantis::asset_cooker::parseDdsBc7(reinterpret_cast<const std::uint8_t*>(dds.data()), dds.size());
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().width == 4);
  CHECK(parsed.value().height == 4);
  CHECK_FALSE(parsed.value().srgb);
  CHECK(parsed.value().baseMipBlockBytes.size() == 16);
  const std::string manifest = readText(run.outputDir / "cook_manifest.txt");
  CHECK(manifest.find("--kind=texture --source={import_dir}/t/_importer/white_4x4_bc7.dds --asset-root={import_dir}") !=
        std::string::npos);
  CHECK(manifest.find("--kind=material --source={import_dir}/t/materials/0.material.txt --asset-root={import_dir}") !=
        std::string::npos);
  CHECK(manifest.find("--kind=texture") < manifest.find("--kind=material"));
}

TEST_CASE("A spec-gloss material with a specularGlossinessTexture takes the Ruling 2 dielectric fallback",
          "[gltf_importer][material]") {
  auto spec = gltf_test::unitQuad();
  addDdsTexture(spec, "brick_diff.dds");
  spec.materialsJson =
      "[{\"extensions\":{\"KHR_materials_pbrSpecularGlossiness\":{\"diffuseFactor\":[0.9,0.8,0.7,1.0],"
      "\"glossinessFactor\":0.3,\"diffuseTexture\":{\"index\":0},\"specularGlossinessTexture\":{\"index\":0}}}}]";
  const MaterialRun run = runMaterialImport("material_sg_texture_fallback", spec, {"brick_diff.dds"});
  REQUIRE(run.result.isOk());
  const GltfImportSummary& summary = run.result.value();
  CHECK(summary.materialsSpecGlossTextureFallback == 1);
  CHECK(summary.materialsSpecGlossFormula == 0);
  CHECK(reportContains(summary, "specularGlossinessTexture dropped; dielectric fallback metallic=0 roughness=0.7"));

  const auto material = parsedMaterial0(run.outputDir);
  CHECK(material.metallicFactor == 0.0f);
  CHECK(material.roughnessFactor == Catch::Approx(0.7f));
  CHECK(material.baseColorFactor[0] == Catch::Approx(0.9f));
  CHECK(material.baseColorFactor[2] == Catch::Approx(0.7f));
  // Texture logical path = <content-root dir name>/<uri>; sampler absent ->
  // glTF default, linear/repeat.
  CHECK(material.textureLogicalPath == "material_sg_texture_fallback/brick_diff.dds");
  CHECK(material.filter == atlantis::asset_system::MaterialSamplerFilter::Linear);
  CHECK(material.addressMode == atlantis::asset_system::MaterialSamplerAddressMode::Repeat);

  // Ruling 4: *_diff* stored as BC7_UNORM (DXGI 99) is reported, not changed.
  CHECK(summary.colorSpaceWarnings == 1);
  CHECK(reportContains(summary, "colour-space warning (Ruling 4): material_sg_texture_fallback/brick_diff.dds"));
  const std::string manifest = readText(run.outputDir / "cook_manifest.txt");
  CHECK(manifest.find("--source={content_parent}/material_sg_texture_fallback/brick_diff.dds "
                      "--asset-root={content_parent}") != std::string::npos);
}

TEST_CASE("A transmission material imports as pbr_direct_lit with a report line", "[gltf_importer][material]") {
  auto spec = gltf_test::unitQuad();
  spec.materialsJson =
      "[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.9,0.9,0.9,1.0],\"metallicFactor\":0.0,"
      "\"roughnessFactor\":0.1},\"alphaMode\":\"BLEND\",\"doubleSided\":true,"
      "\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":0.95}}}]";
  spec.extensionsUsedJson = "[\"KHR_materials_transmission\"]";
  const MaterialRun run = runMaterialImport("material_transmission", spec);
  REQUIRE(run.result.isOk());
  const GltfImportSummary& summary = run.result.value();
  CHECK(summary.materialsTransmission == 1);
  CHECK(summary.materialsMetallicRoughness == 1);
  CHECK(reportContains(summary, "KHR_materials_transmission factor 0.95 recorded only; imported as pbr_direct_lit"));
  CHECK(reportContains(summary, "no v6 destination, dropped (Ruling 3): alphaMode=BLEND doubleSided"));
  const auto material = parsedMaterial0(run.outputDir);
  CHECK(material.kind == atlantis::asset_system::MaterialKind::PbrDirectLit);
  CHECK(material.metallicFactor == 0.0f);
  CHECK(material.roughnessFactor == Catch::Approx(0.1f));
}

TEST_CASE("Generated .material.txt round-trips through parseMaterialSource and cooks through cookMaterial",
          "[gltf_importer][material]") {
  auto spec = gltf_test::unitQuad();
  addDdsTexture(spec, "wall_diff.dds");
  spec.imagesJson = "[{\"uri\":\"missing.png\"},{\"uri\":\"wall_diff.dds\"},{\"uri\":\"wall_ddna.dds\"}]";
  spec.texturesJson =
      "[{\"source\":0,\"extensions\":{\"MSFT_texture_dds\":{\"source\":1}}},"
      "{\"source\":0,\"extensions\":{\"MSFT_texture_dds\":{\"source\":2}}}]";
  spec.materialsJson =
      "[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.25,0.5,0.75,1.0],\"metallicFactor\":0.125,"
      "\"roughnessFactor\":0.625,\"baseColorTexture\":{\"index\":0}},\"normalTexture\":{\"index\":1}}]";
  const MaterialRun run = runMaterialImport("material_round_trip", spec, {"wall_diff.dds", "wall_ddna.dds"});
  REQUIRE(run.result.isOk());

  const fs::path sourcePath = run.outputDir / "t/materials/0.material.txt";
  const std::string text = readText(sourcePath);
  const auto material = parsedMaterial0(run.outputDir);
  CHECK(atlantis::asset_system::serializeMaterialSource(material) == text);
  CHECK(material.baseColorFactor[0] == 0.25f);
  CHECK(material.baseColorFactor[1] == 0.5f);
  CHECK(material.baseColorFactor[2] == 0.75f);
  CHECK(material.metallicFactor == 0.125f);
  CHECK(material.roughnessFactor == 0.625f);
  CHECK(material.textureLogicalPath == "material_round_trip/wall_diff.dds");
  CHECK(material.normalMapLogicalPath == "material_round_trip/wall_ddna.dds");

  const auto cooked = atlantis::asset_system::cookMaterial(sourcePath.string(), "t/materials/0.material.txt",
                                                           (run.dir / "cooked/0.amaterial").string(),
                                                           (run.dir / "cooked/0.amaterial.meta.txt").string());
  CHECK(cooked.isOk());
}

TEST_CASE("Out-of-scope material content fails with distinct named errors and leaves no output",
          "[gltf_importer][material]") {
  {
    auto spec = gltf_test::unitQuad();
    spec.materialsJson = "[{\"extensions\":{\"KHR_materials_clearcoat\":{\"clearcoatFactor\":1.0}}}]";
    spec.extensionsUsedJson = "[\"KHR_materials_clearcoat\"]";
    const MaterialRun run = runMaterialImport("material_reject_extension", spec);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::UnsupportedMaterialExtension);
    CHECK_FALSE(fs::exists(run.outputDir));
  }
  {
    auto spec = gltf_test::unitQuad();
    addDdsTexture(spec, "tile_diff.dds", "0");
    spec.samplersJson = "[{\"wrapS\":33648,\"wrapT\":33648}]";
    spec.materialsJson = "[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}]";
    const MaterialRun run = runMaterialImport("material_reject_mirrored", spec, {"tile_diff.dds"});
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::UnsupportedSamplerWrap);
    CHECK_FALSE(fs::exists(run.outputDir));
  }
  {
    auto spec = gltf_test::unitQuad();
    addDdsTexture(spec, "absent_diff.dds");
    spec.materialsJson = "[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}]";
    const MaterialRun run = runMaterialImport("material_reject_missing_dds", spec);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::MissingTextureFile);
    CHECK_FALSE(fs::exists(run.outputDir));
  }
}
