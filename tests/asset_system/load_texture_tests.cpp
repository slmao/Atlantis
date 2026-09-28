#include <atlantis/asset_system/load_texture.h>

#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/texture_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include <atlantis/asset_system/asset_guid.h>
#include <string_view>

namespace {

// Plan 0047 M3: a deterministic, non-nil test identity per logical path, so
// a test's cross-references (scene -> mesh, material -> texture) agree.
[[nodiscard]] atlantis::asset_system::AssetGuid testAssetGuid(std::string_view key) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), key);
}

}  // namespace
using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

// Per-process tag: catch_discover_tests runs each TEST_CASE in its own
// process under ctest -j, and the counter below restarts at 0 in each.
const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_system_load_texture_tests" /
              (label + "_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
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
                                   testAssetGuid("textures/checker.png"),
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
                                        testAssetGuid("textures/other.png"),
                                        otherArtifactPath, otherMetadataPath);
  REQUIRE(otherResult.isOk());

  fs::copy_file(otherMetadataPath, metadataPath, fs::copy_options::overwrite_existing);

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataArtifactMismatch);
}

TEST_CASE(
    "loadTextureAsset detects a metadata file whose asset_id is not the key of its asset_guid, even when "
    "width/height/format still match the artifact",
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

  // Plan 0047 P8 (ADR-0097 D2/D3): the source path is provenance only, so a
  // different one still loads...
  const std::string oldLine = "source_logical_path: textures/checker.png";
  const std::string newLine = "source_logical_path: some/other/path.png";
  const auto pathPos = metadataText.find(oldLine);
  REQUIRE(pathPos != std::string::npos);
  metadataText.replace(pathPos, oldLine.size(), newLine);
  writeFile(metadataPath, metadataText);
  CHECK(loadTextureAsset(artifactPath, metadataPath).isOk());

  // ...but an asset_id that is not the key of the recorded asset_guid is an
  // internal contradiction the artifact-vs-metadata check alone cannot see.
  const std::string guidPrefix = "asset_guid: ";
  const auto guidPos = metadataText.find(guidPrefix);
  REQUIRE(guidPos != std::string::npos);
  metadataText.replace(guidPos + guidPrefix.size(), 36, "fedcba98-7654-4321-8fed-cba987654321");
  writeFile(metadataPath, metadataText);

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataArtifactMismatch);
}

// Spec 0045: the loaded shape carries the chain and its count, and the
// loader cross-checks the count against the metadata sidecar.
TEST_CASE("loadTextureAsset returns a BC7 mip chain with its count", "[asset_system][mip]") {
  TempDirGuard dir("bc7_chain");
  std::vector<std::uint8_t> chain(static_cast<std::size_t>(textureMipChainByteCount(8, 8, TextureDataLayout::Bc7, 4)));
  for (std::size_t i = 0; i < chain.size(); ++i) chain[i] = static_cast<std::uint8_t>(i % 251);
  const fs::path artifactPath = dir.path / "chain.atex";
  const fs::path metadataPath = dir.path / "chain.atex.meta.txt";
  REQUIRE(cookTextureBc7(chain.data(), chain.size(), 8, 8, 4, TextureColorSpace::Srgb, "textures/chain.dds",
                         testAssetGuid("textures/chain.dds"),
                         artifactPath, metadataPath)
              .isOk());

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().mipCount == 4);
  CHECK(result.value().layout == TextureDataLayout::Bc7);
  CHECK(result.value().pixelBytes == chain);

  // A PNG-sourced texture is always one level (Spec 0045 R6).
  TempDirGuard pngDir("png_single_level");
  const auto [pngArtifact, pngMetadata] = cookValidChecker(pngDir.path);
  const auto png = loadTextureAsset(pngArtifact, pngMetadata);
  REQUIRE(png.isOk());
  CHECK(png.value().mipCount == 1);
}

TEST_CASE("loadTextureAsset rejects a metadata mip_count that disagrees with the artifact", "[asset_system][mip]") {
  TempDirGuard dir("mip_mismatch");
  std::vector<std::uint8_t> chain(static_cast<std::size_t>(textureMipChainByteCount(8, 8, TextureDataLayout::Bc7, 4)));
  const fs::path artifactPath = dir.path / "chain.atex";
  const fs::path metadataPath = dir.path / "chain.atex.meta.txt";
  REQUIRE(cookTextureBc7(chain.data(), chain.size(), 8, 8, 4, TextureColorSpace::Unorm, "textures/chain.dds",
                         testAssetGuid("textures/chain.dds"),
                         artifactPath, metadataPath)
              .isOk());

  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  const std::string oldLine = "mip_count: 4";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), "mip_count: 3");
  writeFile(metadataPath, metadataText);

  const auto result = loadTextureAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureLoadError::MetadataArtifactMismatch);
}
