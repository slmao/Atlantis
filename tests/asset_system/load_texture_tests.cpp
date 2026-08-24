#include <atlantis/asset_system/load_texture.h>

#include <atlantis/asset_system/cook_texture.h>

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
      : path(fs::temp_directory_path() / "atlantis_asset_system_load_texture_tests" /
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

[[nodiscard]] std::vector<std::uint8_t> makeRgbaBytes(std::uint32_t width, std::uint32_t height, std::uint8_t seed) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((i + seed) % 256);
  return bytes;
}

[[nodiscard]] std::pair<fs::path, fs::path> cookValidChecker(const fs::path& dir) {
  const auto pixels = makeRgbaBytes(4, 4, 11);
  const fs::path artifactPath = dir / "checker.atex";
  const fs::path metadataPath = dir / "checker.atex.meta.txt";
  const auto result = cookTexture(pixels.data(), 4, 4, 4, TextureColorSpace::Unorm, "textures/checker.png",
                                   artifactPath, metadataPath);
  REQUIRE(result.isOk());
  return {artifactPath, metadataPath};
}

void writeFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

}  // namespace

TEST_CASE("loadTextureAsset loads a well-formed artifact/metadata pair", "[asset_system]") {
  TempDirGuard dir("success");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().width == 4);
  CHECK(result.value().height == 4);
  CHECK(result.value().colorSpace == TextureColorSpace::Unorm);
  CHECK(result.value().pixelBytes == makeRgbaBytes(4, 4, 11));
}

TEST_CASE("loadTextureAsset fails when the artifact file does not exist", "[asset_system]") {
  TempDirGuard dir("missing_artifact");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  const auto result = loadTextureAsset(dir.path / "does_not_exist.atex", metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::ArtifactDecodeFailed);
}

TEST_CASE("loadTextureAsset fails when the metadata file does not exist", "[asset_system]") {
  TempDirGuard dir("missing_metadata");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  const auto result = loadTextureAsset(artifactPath, dir.path / "does_not_exist.meta.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataReadFailed);
}

TEST_CASE("loadTextureAsset fails when the artifact fails to decode", "[asset_system]") {
  TempDirGuard dir("bad_artifact");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  writeFile(artifactPath, "not a valid artifact");

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::ArtifactDecodeFailed);
}

TEST_CASE("loadTextureAsset fails when the metadata fails to parse", "[asset_system]") {
  TempDirGuard dir("bad_metadata");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  writeFile(metadataPath, "not valid metadata\n");

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataParseFailed);
}

TEST_CASE("loadTextureAsset detects a deliberate artifact/metadata mismatch", "[asset_system]") {
  TempDirGuard dir("mismatch");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  // Cook a second, different-sized texture and swap in its metadata --
  // same valid format on both sides, but the recorded width/height now
  // disagree with the artifact's own decoded values.
  const auto otherPixels = makeRgbaBytes(2, 2, 5);
  const fs::path otherArtifactPath = dir.path / "other.atex";
  const fs::path otherMetadataPath = dir.path / "other.atex.meta.txt";
  const auto otherResult = cookTexture(otherPixels.data(), 2, 2, 4, TextureColorSpace::Unorm, "textures/other.png",
                                        otherArtifactPath, otherMetadataPath);
  REQUIRE(otherResult.isOk());

  fs::copy_file(otherMetadataPath, metadataPath, fs::copy_options::overwrite_existing);

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataArtifactMismatch);
}

TEST_CASE(
    "loadTextureAsset detects a metadata file whose own recorded asset_id and source_logical_path disagree with "
    "each other, even when width/height/format still match the artifact",
    "[asset_system]") {
  TempDirGuard dir("self_inconsistent_metadata");
  const auto [artifactPath, metadataPath] = cookValidChecker(dir.path);

  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  const std::string oldLine = "source_logical_path: textures/checker.png";
  const std::string newLine = "source_logical_path: some/other/path.png";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), newLine);
  writeFile(metadataPath, metadataText);

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataArtifactMismatch);
}
