#include <atlantis/asset_system/cook_material.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/material_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
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
      : path(fs::temp_directory_path() / "atlantis_cook_material_tests" /
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

[[nodiscard]] std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

constexpr std::string_view kValidSource =
    "atlantis_material_source_version: 2\n"
    "kind: unlit_textured\n"
    "texture: textures/textured_quad_source_unorm.png\n"
    "filter: linear\n"
    "address_mode: repeat\n";

constexpr std::string_view kValidPbrSource =
    "atlantis_material_source_version: 2\n"
    "kind: pbr_direct_lit\n"
    "texture: textures/textured_quad_source_srgb.png\n"
    "filter: linear\n"
    "address_mode: repeat\n"
    "base_color_factor: 0.8 0.2 0.1 1.0\n"
    "metallic_factor: 0.5\n"
    "roughness_factor: 0.25\n";

}  // namespace

TEST_CASE("cookMaterial writes a well-formed artifact/metadata pair", "[asset_system][material]") {
  TempDirGuard dir("success");
  const fs::path sourcePath = dir.path / "unlit_textured_quad.material.txt";
  writeFile(sourcePath, std::string(kValidSource));

  const fs::path artifactPath = dir.path / "unlit_textured_quad.amaterial";
  const fs::path metadataPath = dir.path / "unlit_textured_quad.amaterial.meta.txt";

  const auto result = cookMaterial(sourcePath.string(), "materials/unlit_textured_quad.material.txt",
                                    artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());
  REQUIRE(fs::exists(artifactPath));
  REQUIRE(fs::exists(metadataPath));

  const std::string artifactText = readFile(artifactPath);
  std::vector<std::byte> artifactBytes(artifactText.size());
  for (std::size_t i = 0; i < artifactText.size(); ++i) {
    artifactBytes[i] = static_cast<std::byte>(static_cast<unsigned char>(artifactText[i]));
  }
  const auto decoded = decodeMaterialArtifact(artifactBytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::UnlitTextured);
  CHECK(decoded.value().textureAsset == computeAssetId("textures/textured_quad_source_unorm.png"));
  CHECK(decoded.value().filter == MaterialSamplerFilter::Linear);
  CHECK(decoded.value().addressMode == MaterialSamplerAddressMode::Repeat);

  const auto metadata = parseMaterialMetadata(readFile(metadataPath));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().sourceLogicalPath == "materials/unlit_textured_quad.material.txt");
  CHECK(metadata.value().assetId == computeAssetId("materials/unlit_textured_quad.material.txt"));
  CHECK(metadata.value().kind == MaterialKind::UnlitTextured);
  CHECK(metadata.value().textureAsset == computeAssetId("textures/textured_quad_source_unorm.png"));
}

TEST_CASE("cookMaterial reports SourceFileUnreadable for a missing source file", "[asset_system][material]") {
  TempDirGuard dir("source_unreadable");
  const auto result = cookMaterial((dir.path / "does_not_exist.material.txt").string(), "materials/foo.material.txt",
                                    (dir.path / "a.amaterial").string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::SourceFileUnreadable);
}

TEST_CASE("cookMaterial reports SourceParseFailed for a malformed source", "[asset_system][material]") {
  TempDirGuard dir("source_parse_failed");
  const fs::path sourcePath = dir.path / "bad.material.txt";
  writeFile(sourcePath, "not a valid material source\n");

  const auto result = cookMaterial(sourcePath.string(), "materials/bad.material.txt",
                                    (dir.path / "a.amaterial").string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::SourceParseFailed);
}

TEST_CASE("cookMaterial rejects every malformed logical path normalizeLogicalPath() itself rejects",
          "[asset_system][material]") {
  TempDirGuard dir("logical_path_invalid");
  const fs::path sourcePath = dir.path / "unlit_textured_quad.material.txt";
  writeFile(sourcePath, std::string(kValidSource));

  const auto reject = [&](const std::string& malformedPath) {
    const auto result = cookMaterial(sourcePath.string(), malformedPath, (dir.path / "a.amaterial").string(),
                                      (dir.path / "a.amaterial.meta.txt").string());
    REQUIRE(result.isErr());
    CHECK(result.error() == MaterialCookError::LogicalPathInvalid);
    CHECK_FALSE(fs::exists(dir.path / "a.amaterial"));
  };

  SECTION("empty path") { reject(""); }
  SECTION("absolute POSIX-style path") { reject("/materials/a.material.txt"); }
  SECTION("Windows drive-letter prefix") { reject("C:\\materials\\a.material.txt"); }
  SECTION("'..' escaping the asset root") { reject("../a.material.txt"); }
}

TEST_CASE("cookMaterial rejects a source naming a texture with a malformed logical path",
          "[asset_system][material]") {
  TempDirGuard dir("texture_logical_path_invalid");
  const fs::path sourcePath = dir.path / "bad_texture_ref.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: unlit_textured\n"
            "texture: /absolute/not/allowed.png\n"
            "filter: linear\n"
            "address_mode: repeat\n");

  const auto result = cookMaterial(sourcePath.string(), "materials/bad_texture_ref.material.txt",
                                    (dir.path / "a.amaterial").string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::LogicalPathInvalid);
}

TEST_CASE("cookMaterial reports AtomicWriteFailed when the artifact output path is a directory",
          "[asset_system][material]") {
  TempDirGuard dir("atomic_write_failed");
  const fs::path sourcePath = dir.path / "unlit_textured_quad.material.txt";
  writeFile(sourcePath, std::string(kValidSource));

  const fs::path artifactPath = dir.path / "a.amaterial";
  fs::create_directories(artifactPath);

  const auto result = cookMaterial(sourcePath.string(), "materials/unlit_textured_quad.material.txt",
                                    artifactPath.string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::AtomicWriteFailed);
}

TEST_CASE("cookMaterial is deterministic -- cooking the same source twice produces identical output",
          "[asset_system][material]") {
  TempDirGuard dir("determinism");
  const fs::path sourcePath = dir.path / "unlit_textured_quad.material.txt";
  writeFile(sourcePath, std::string(kValidSource));

  const auto firstResult =
      cookMaterial(sourcePath.string(), "materials/unlit_textured_quad.material.txt",
                   (dir.path / "first.amaterial").string(), (dir.path / "first.amaterial.meta.txt").string());
  REQUIRE(firstResult.isOk());
  const auto secondResult =
      cookMaterial(sourcePath.string(), "materials/unlit_textured_quad.material.txt",
                   (dir.path / "second.amaterial").string(), (dir.path / "second.amaterial.meta.txt").string());
  REQUIRE(secondResult.isOk());

  CHECK(readFile(dir.path / "first.amaterial") == readFile(dir.path / "second.amaterial"));
  CHECK(readFile(dir.path / "first.amaterial.meta.txt") == readFile(dir.path / "second.amaterial.meta.txt"));
}

TEST_CASE("cookMaterial writes a well-formed PbrDirectLit artifact/metadata pair with its own PBR parameters",
          "[asset_system][material]") {
  TempDirGuard dir("pbr_success");
  const fs::path sourcePath = dir.path / "pbr_dielectric_rough.material.txt";
  writeFile(sourcePath, std::string(kValidPbrSource));

  const fs::path artifactPath = dir.path / "pbr_dielectric_rough.amaterial";
  const fs::path metadataPath = dir.path / "pbr_dielectric_rough.amaterial.meta.txt";

  const auto result = cookMaterial(sourcePath.string(), "materials/pbr_dielectric_rough.material.txt",
                                    artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());

  const std::string artifactText = readFile(artifactPath);
  std::vector<std::byte> artifactBytes(artifactText.size());
  for (std::size_t i = 0; i < artifactText.size(); ++i) {
    artifactBytes[i] = static_cast<std::byte>(static_cast<unsigned char>(artifactText[i]));
  }
  const auto decoded = decodeMaterialArtifact(artifactBytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::PbrDirectLit);
  CHECK(decoded.value().baseColorFactor[0] == 0.8f);
  CHECK(decoded.value().baseColorFactor[1] == 0.2f);
  CHECK(decoded.value().baseColorFactor[2] == 0.1f);
  CHECK(decoded.value().baseColorFactor[3] == 1.0f);
  CHECK(decoded.value().metallicFactor == 0.5f);
  CHECK(decoded.value().roughnessFactor == 0.25f);

  const auto metadata = parseMaterialMetadata(readFile(metadataPath));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().kind == MaterialKind::PbrDirectLit);
  CHECK(metadata.value().baseColorFactor[0] == 0.8f);
  CHECK(metadata.value().metallicFactor == 0.5f);
  CHECK(metadata.value().roughnessFactor == 0.25f);
}

TEST_CASE("cookMaterial reports BaseColorFactorOutOfRange for a baseColorFactor component above 1.0",
          "[asset_system][material]") {
  TempDirGuard dir("base_color_out_of_range");
  const fs::path sourcePath = dir.path / "bad.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: pbr_direct_lit\n"
            "texture: textures/textured_quad_source_srgb.png\n"
            "filter: linear\n"
            "address_mode: repeat\n"
            "base_color_factor: 1.5 0.2 0.1 1.0\n"
            "metallic_factor: 0.5\n"
            "roughness_factor: 0.25\n");

  const auto result = cookMaterial(sourcePath.string(), "materials/bad.material.txt",
                                    (dir.path / "a.amaterial").string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::BaseColorFactorOutOfRange);
  CHECK_FALSE(fs::exists(dir.path / "a.amaterial"));
}

TEST_CASE("cookMaterial reports MaterialFactorOutOfRange for a negative metallic_factor",
          "[asset_system][material]") {
  TempDirGuard dir("metallic_out_of_range");
  const fs::path sourcePath = dir.path / "bad.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: pbr_direct_lit\n"
            "texture: textures/textured_quad_source_srgb.png\n"
            "filter: linear\n"
            "address_mode: repeat\n"
            "base_color_factor: 1.0 1.0 1.0 1.0\n"
            "metallic_factor: -0.1\n"
            "roughness_factor: 0.25\n");

  const auto result = cookMaterial(sourcePath.string(), "materials/bad.material.txt",
                                    (dir.path / "a.amaterial").string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::MaterialFactorOutOfRange);
  CHECK_FALSE(fs::exists(dir.path / "a.amaterial"));
}

TEST_CASE("cookMaterial reports MaterialFactorOutOfRange for a roughness_factor above 1.0",
          "[asset_system][material]") {
  TempDirGuard dir("roughness_out_of_range");
  const fs::path sourcePath = dir.path / "bad.material.txt";
  writeFile(sourcePath,
            "atlantis_material_source_version: 2\n"
            "kind: pbr_direct_lit\n"
            "texture: textures/textured_quad_source_srgb.png\n"
            "filter: linear\n"
            "address_mode: repeat\n"
            "base_color_factor: 1.0 1.0 1.0 1.0\n"
            "metallic_factor: 0.5\n"
            "roughness_factor: 1.1\n");

  const auto result = cookMaterial(sourcePath.string(), "materials/bad.material.txt",
                                    (dir.path / "a.amaterial").string(), (dir.path / "a.amaterial.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialCookError::MaterialFactorOutOfRange);
  CHECK_FALSE(fs::exists(dir.path / "a.amaterial"));
}
