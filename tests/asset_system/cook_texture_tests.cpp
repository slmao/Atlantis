#include <atlantis/asset_system/cook_texture.h>

#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/texture_metadata.h>

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
      : path(fs::temp_directory_path() / "atlantis_asset_system_cook_texture_tests" /
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

[[nodiscard]] std::vector<std::uint8_t> makeRgbaBytes(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((i * 7 + 3) % 256);
  return bytes;
}

[[nodiscard]] std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

TEST_CASE("cookTexture writes a well-formed artifact/metadata pair", "[asset_system]") {
  TempDirGuard dir("success");
  const auto pixels = makeRgbaBytes(4, 4);
  const fs::path artifactPath = dir.path / "checker.atex";
  const fs::path metadataPath = dir.path / "checker.atex.meta.txt";

  const auto result = cookTexture(pixels.data(), 4, 4, 3, TextureColorSpace::Srgb, "textures/checker.png",
                                   artifactPath, metadataPath);
  REQUIRE(result.isOk());
  REQUIRE(fs::exists(artifactPath));
  REQUIRE(fs::exists(metadataPath));

  const std::string artifactText = readFile(artifactPath);
  std::vector<std::byte> artifactBytes(artifactText.size());
  for (std::size_t i = 0; i < artifactText.size(); ++i) {
    artifactBytes[i] = static_cast<std::byte>(static_cast<unsigned char>(artifactText[i]));
  }
  const auto decoded = decodeTextureArtifact(artifactBytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().width == 4);
  CHECK(decoded.value().height == 4);
  CHECK(decoded.value().colorSpace == TextureColorSpace::Srgb);
  CHECK(decoded.value().pixelBytes == pixels);

  const auto metadata = parseTextureMetadata(readFile(metadataPath));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().sourceLogicalPath == "textures/checker.png");
  CHECK(metadata.value().width == 4);
  CHECK(metadata.value().height == 4);
  CHECK(metadata.value().format == TextureColorSpace::Srgb);
  CHECK(metadata.value().channelsInFile == 3);
  CHECK(metadata.value().assetId == computeAssetId("textures/checker.png"));
}

TEST_CASE("cookTexture rejects a zero width or height", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  TempDirGuard dir("zero_dimension");

  SECTION("zero width") {
    const auto result = cookTexture(pixels.data(), 0, 1, 4, TextureColorSpace::Unorm, "a.png",
                                     dir.path / "a.atex", dir.path / "a.atex.meta.txt");
    REQUIRE(result.isErr());
    CHECK(result.error() == TextureCookError::ZeroDimension);
  }

  SECTION("zero height") {
    const auto result = cookTexture(pixels.data(), 1, 0, 4, TextureColorSpace::Unorm, "a.png",
                                     dir.path / "a.atex", dir.path / "a.atex.meta.txt");
    REQUIRE(result.isErr());
    CHECK(result.error() == TextureCookError::ZeroDimension);
  }
}

TEST_CASE("cookTexture rejects a dimension exceeding kMaxTextureDimension", "[asset_system]") {
  TempDirGuard dir("dimension_exceeds_maximum");
  const auto pixels = makeRgbaBytes(1, 1);
  const auto result = cookTexture(pixels.data(), kMaxTextureDimension + 1, 1, 4, TextureColorSpace::Unorm, "a.png",
                                   dir.path / "a.atex", dir.path / "a.atex.meta.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureCookError::DimensionExceedsMaximum);
}

TEST_CASE("cookTexture reports AtomicWriteFailed when the artifact output path is a directory", "[asset_system]") {
  TempDirGuard dir("atomic_write_failed");
  const auto pixels = makeRgbaBytes(1, 1);
  const fs::path artifactPath = dir.path / "a.atex";
  fs::create_directories(artifactPath);

  const auto result = cookTexture(pixels.data(), 1, 1, 4, TextureColorSpace::Unorm, "a.png", artifactPath,
                                   dir.path / "a.atex.meta.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureCookError::AtomicWriteFailed);
}

TEST_CASE("cookTexture is deterministic -- cooking the same decoded bytes twice produces identical artifact bytes",
          "[asset_system]") {
  TempDirGuard dir("determinism");
  const auto pixels = makeRgbaBytes(8, 8);

  const auto firstResult = cookTexture(pixels.data(), 8, 8, 4, TextureColorSpace::Unorm, "a.png",
                                        dir.path / "first.atex", dir.path / "first.atex.meta.txt");
  REQUIRE(firstResult.isOk());
  const auto secondResult = cookTexture(pixels.data(), 8, 8, 4, TextureColorSpace::Unorm, "a.png",
                                         dir.path / "second.atex", dir.path / "second.atex.meta.txt");
  REQUIRE(secondResult.isOk());

  CHECK(readFile(dir.path / "first.atex") == readFile(dir.path / "second.atex"));
  CHECK(readFile(dir.path / "first.atex.meta.txt") == readFile(dir.path / "second.atex.meta.txt"));
}
