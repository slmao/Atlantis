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

// Plan 0016's own "Human Review Correction -- 2026-08-24" (Verification
// V47): cookTexture() now normalizes logicalPathInput via the same
// normalizeLogicalPath() cookStaticMesh() already calls, so every
// malformed form logical_path_tests.cpp already proves normalizeLogicalPath()
// itself rejects must be rejected here too, via the new
// TextureCookError::LogicalPathInvalid -- not silently accepted the way
// the pre-correction cookTexture() (which never normalized its input at
// all) would have.
TEST_CASE("cookTexture rejects every malformed logical path normalizeLogicalPath() itself rejects",
          "[asset_system]") {
  TempDirGuard dir("logical_path_invalid");
  const auto pixels = makeRgbaBytes(1, 1);

  const auto reject = [&](const std::string& malformedPath) {
    const auto result = cookTexture(pixels.data(), 1, 1, 4, TextureColorSpace::Unorm, malformedPath,
                                     dir.path / "a.atex", dir.path / "a.atex.meta.txt");
    REQUIRE(result.isErr());
    CHECK(result.error() == TextureCookError::LogicalPathInvalid);
    CHECK_FALSE(fs::exists(dir.path / "a.atex"));
  };

  SECTION("empty path") { reject(""); }
  SECTION("path that normalizes to nothing") { reject("."); }
  SECTION("absolute POSIX-style path") { reject("/textures/a.png"); }
  SECTION("UNC-style path") { reject("\\\\server\\share\\textures\\a.png"); }
  SECTION("Windows drive-letter prefix") { reject("C:\\textures\\a.png"); }
  SECTION("'..' escaping the asset root") { reject("../a.png"); }
  SECTION("non-ASCII byte") { reject("textures/caf\xC3\xA9.png"); }
  SECTION("disallowed colon not in drive-letter position") { reject("textures/foo:bar.png"); }
}

TEST_CASE("cookTexture accepts a not-yet-normalized logical path, cooking under its normalized form",
          "[asset_system]") {
  // Mirrors cookStaticMesh()'s own established shape: normalization
  // happens inside cookTexture() itself, so a caller-supplied path that
  // merely needs backslash/redundant-separator cleanup still succeeds,
  // and both the recorded AssetId and sourceLogicalPath reflect the
  // normalized form, never the raw caller input.
  TempDirGuard dir("logical_path_normalized");
  const auto pixels = makeRgbaBytes(1, 1);

  const auto result = cookTexture(pixels.data(), 1, 1, 4, TextureColorSpace::Unorm, "textures\\a.png",
                                   dir.path / "a.atex", dir.path / "a.atex.meta.txt");
  REQUIRE(result.isOk());

  const auto metadata = parseTextureMetadata(readFile(dir.path / "a.atex.meta.txt"));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().sourceLogicalPath == "textures/a.png");
  CHECK(metadata.value().assetId == computeAssetId("textures/a.png"));
}
