#include <atlantis/asset_system/load_material.h>

#include <atlantis/asset_system/cook_material.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_system_load_material_tests" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

constexpr std::string_view kValidSource =
    "atlantis_material_source_version: 2\n"
    "kind: unlit_textured\n"
    "texture: textures/textured_quad_source_unorm.png\n"
    "filter: linear\n"
    "address_mode: repeat\n";

[[nodiscard]] std::pair<fs::path, fs::path> cookValidMaterial(const fs::path& dir) {
  const fs::path sourcePath = dir / "unlit_textured_quad.material.txt";
  writeFile(sourcePath, std::string(kValidSource));

  const fs::path artifactPath = dir / "unlit_textured_quad.amaterial";
  const fs::path metadataPath = dir / "unlit_textured_quad.amaterial.meta.txt";
  const auto result = cookMaterial(sourcePath.string(), "materials/unlit_textured_quad.material.txt",
                                    artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());
  return {artifactPath, metadataPath};
}

}  // namespace

TEST_CASE("loadMaterialAsset loads a well-formed artifact/metadata pair", "[asset_system][material]") {
  TempDirGuard dir("success");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().kind == MaterialKind::UnlitTextured);
  CHECK(result.value().textureAsset == computeAssetId("textures/textured_quad_source_unorm.png"));
  CHECK(result.value().filter == MaterialSamplerFilter::Linear);
  CHECK(result.value().addressMode == MaterialSamplerAddressMode::Repeat);
  CHECK(result.value().baseColorFactor[0] == 1.0f);
  CHECK(result.value().metallicFactor == 1.0f);
  CHECK(result.value().roughnessFactor == 1.0f);
}

TEST_CASE("loadMaterialAsset loads a well-formed PbrDirectLit material with its own PBR parameters",
          "[asset_system][material]") {
  TempDirGuard dir("pbr_success");
  const fs::path sourcePath = dir.path / "pbr_dielectric_rough.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: pbr_direct_lit\n"
            "texture: textures/textured_quad_source_srgb.png\n"
            "filter: linear\n"
            "address_mode: repeat\n"
            "base_color_factor: 0.8 0.2 0.1 1.0\n"
            "metallic_factor: 0.5\n"
            "roughness_factor: 0.25\n");
  const fs::path artifactPath = dir.path / "pbr_dielectric_rough.amaterial";
  const fs::path metadataPath = dir.path / "pbr_dielectric_rough.amaterial.meta.txt";
  const auto cookResult = cookMaterial(sourcePath.string(), "materials/pbr_dielectric_rough.material.txt",
                                        artifactPath.string(), metadataPath.string());
  REQUIRE(cookResult.isOk());

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().kind == MaterialKind::PbrDirectLit);
  CHECK(result.value().baseColorFactor[0] == 0.8f);
  CHECK(result.value().baseColorFactor[1] == 0.2f);
  CHECK(result.value().baseColorFactor[2] == 0.1f);
  CHECK(result.value().baseColorFactor[3] == 1.0f);
  CHECK(result.value().metallicFactor == 0.5f);
  CHECK(result.value().roughnessFactor == 0.25f);
}

// Real, disclosed regression coverage (found during Plan 0023's own
// final review): a non-"round" float value (one std::to_string(float)'s
// fixed 6-decimal-place formatting does NOT round-trip exactly, unlike
// every other value this test file's own literals happen to use) --
// this would have failed with MetadataArtifactMismatch against
// serializeMaterialMetadata()'s own former std::to_string()-based
// formatting (metadata.cpp's own text round-trip loses precision;
// load_material.cpp's own exact-equality artifact-vs-metadata check
// then spuriously rejects a legitimately, self-consistently cooked
// material), proving the fix (std::to_chars(), a shortest-round-trip
// guarantee) is real, not merely decorative.
TEST_CASE("loadMaterialAsset round-trips a metallic_factor value that std::to_string(float) would NOT preserve "
          "through the metadata sidecar's own text encoding",
          "[asset_system][material]") {
  TempDirGuard dir("pbr_precise_float_roundtrip");
  const fs::path sourcePath = dir.path / "pbr_precise.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: pbr_direct_lit\n"
            "texture: textures/textured_quad_source_srgb.png\n"
            "filter: linear\n"
            "address_mode: repeat\n"
            "base_color_factor: 1.0 1.0 1.0 1.0\n"
            "metallic_factor: 0.123456789\n"
            "roughness_factor: 0.333333333\n");
  const fs::path artifactPath = dir.path / "pbr_precise.amaterial";
  const fs::path metadataPath = dir.path / "pbr_precise.amaterial.meta.txt";
  const auto cookResult = cookMaterial(sourcePath.string(), "materials/pbr_precise.material.txt",
                                        artifactPath.string(), metadataPath.string());
  REQUIRE(cookResult.isOk());

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  // The exact float32 nearest to the source text's own decimal literal
  // -- confirms the metadata sidecar's own round-trip preserved the
  // precise bit pattern, not merely "close enough".
  float expectedMetallic = 0.0f;
  const std::string_view metallicText = "0.123456789";
  std::from_chars(metallicText.data(), metallicText.data() + metallicText.size(), expectedMetallic);
  float expectedRoughness = 0.0f;
  const std::string_view roughnessText = "0.333333333";
  std::from_chars(roughnessText.data(), roughnessText.data() + roughnessText.size(), expectedRoughness);
  CHECK(result.value().metallicFactor == expectedMetallic);
  CHECK(result.value().roughnessFactor == expectedRoughness);
}

TEST_CASE("loadMaterialAsset detects a metadata/artifact mismatch scoped to metallicFactor alone",
          "[asset_system][material]") {
  TempDirGuard dir("metallic_mismatch");
  const fs::path sourcePath = dir.path / "pbr.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: pbr_direct_lit\n"
            "texture: textures/textured_quad_source_srgb.png\n"
            "filter: linear\n"
            "address_mode: repeat\n"
            "base_color_factor: 1.0 1.0 1.0 1.0\n"
            "metallic_factor: 0.5\n"
            "roughness_factor: 0.25\n");
  const fs::path artifactPath = dir.path / "pbr.amaterial";
  const fs::path metadataPath = dir.path / "pbr.amaterial.meta.txt";
  const auto cookResult = cookMaterial(sourcePath.string(), "materials/pbr.material.txt", artifactPath.string(),
                                        metadataPath.string());
  REQUIRE(cookResult.isOk());

  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  // Plan 0023's own final review: serializeMaterialMetadata() now formats
  // via formatFloat() (std::to_chars, shortest round-trip), not the former
  // std::to_string()'s fixed 6-decimal-place output -- "0.5", not
  // "0.500000". These literals follow that real, current serialized text.
  const std::string oldLine = "metallic_factor: 0.5";
  const std::string newLine = "metallic_factor: 0.75";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), newLine);
  writeFile(metadataPath, metadataText);

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::MetadataArtifactMismatch);
}

TEST_CASE("loadMaterialAsset fails when the artifact file does not exist", "[asset_system][material]") {
  TempDirGuard dir("missing_artifact");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  const auto result = loadMaterialAsset(dir.path / "does_not_exist.amaterial", metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::ArtifactFileUnreadable);
}

TEST_CASE("loadMaterialAsset fails when the metadata file does not exist", "[asset_system][material]") {
  TempDirGuard dir("missing_metadata");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  const auto result = loadMaterialAsset(artifactPath, dir.path / "does_not_exist.meta.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::MetadataFileUnreadable);
}

TEST_CASE("loadMaterialAsset fails when the artifact fails to decode", "[asset_system][material]") {
  TempDirGuard dir("bad_artifact");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  writeFile(artifactPath, "not a valid artifact");

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::ArtifactDecodeFailed);
}

TEST_CASE("loadMaterialAsset fails when the metadata fails to parse", "[asset_system][material]") {
  TempDirGuard dir("bad_metadata");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  writeFile(metadataPath, "not valid metadata\n");

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::MetadataParseFailed);
}

TEST_CASE("loadMaterialAsset detects a deliberate artifact/metadata mismatch", "[asset_system][material]") {
  TempDirGuard dir("mismatch");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  // Cook a second, different-texture material and swap in its metadata
  // -- same valid format on both sides, but the recorded texture_asset
  // now disagrees with the artifact's own decoded texture_asset_id.
  const fs::path otherSourcePath = dir.path / "other.material.txt";
  writeFile(otherSourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: unlit_textured\n"
            "texture: textures/other.png\n"
            "filter: linear\n"
            "address_mode: repeat\n");
  const fs::path otherArtifactPath = dir.path / "other.amaterial";
  const fs::path otherMetadataPath = dir.path / "other.amaterial.meta.txt";
  const auto otherResult = cookMaterial(otherSourcePath.string(), "materials/other.material.txt",
                                         otherArtifactPath.string(), otherMetadataPath.string());
  REQUIRE(otherResult.isOk());

  fs::copy_file(otherMetadataPath, metadataPath, fs::copy_options::overwrite_existing);

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::MetadataArtifactMismatch);
}

TEST_CASE(
    "loadMaterialAsset detects a metadata file whose own recorded asset_id and source_logical_path disagree with "
    "each other, even when kind/texture_asset still match the artifact",
    "[asset_system][material]") {
  TempDirGuard dir("self_inconsistent_metadata");
  const auto [artifactPath, metadataPath] = cookValidMaterial(dir.path);

  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  const std::string oldLine = "source_logical_path: materials/unlit_textured_quad.material.txt";
  const std::string newLine = "source_logical_path: materials/some_other_material.material.txt";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), newLine);
  writeFile(metadataPath, metadataText);

  const auto result = loadMaterialAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialLoadError::MetadataArtifactMismatch);
}
