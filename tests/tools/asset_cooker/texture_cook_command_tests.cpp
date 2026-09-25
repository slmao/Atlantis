#include <cook_command.h>

#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

// Plan 0016 Section D9 (V-item 7): runCookCommand()'s new texture cook
// mode, exercised through the same public entry point every other cook
// mode already is -- real stbi_load() call, real cookTexture() call, a
// real stamp file. tiny_rgba.png/tiny_rgb.png/tiny_grayscale.png/
// corrupted.png (fixtures/) are tiny, hand-constructed, genuinely valid
// (or, for corrupted.png, deliberately invalid) PNG files checked in for
// this test alone.

using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::runCookCommand;

namespace {

namespace fs = std::filesystem;

// Per-process tag: catch_discover_tests runs each TEST_CASE in its own
// process under ctest -j, and the counter below restarts at 0 in each.
const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_cooker_texture_cmd_tests" /
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

[[nodiscard]] fs::path fixturePath(const char* name) {
  return fs::path(ATLANTIS_ASSET_COOKER_TEST_FIXTURES_DIR) / name;
}

[[nodiscard]] std::string readFileText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Builds a CookCommandRequest for texture mode -- assetRoot is the
// fixture directory itself, so relativePath is just the bare filename;
// stampPath's own stem (name) determines the output artifact/metadata
// basename (runCookTextureMode()'s own documented convention).
[[nodiscard]] CookCommandRequest makeTextureRequest(const fs::path& sourcePng, const fs::path& outputDir,
                                                      const std::string& name, const std::string& colorSpace) {
  CookCommandRequest request;
  request.kind = AssetKind::Texture;
  request.sourcePath = sourcePng.string();
  request.assetRoot = sourcePng.parent_path().string();
  request.outputDir = outputDir.string();
  request.stampPath = (outputDir / (name + ".stamp")).string();
  request.colorSpace = colorSpace;
  return request;
}

}  // namespace

TEST_CASE("runCookCommand cooks a well-formed RGBA texture and writes a stamp", "[asset_cooker][texture]") {
  TempDirGuard dir("rgba_success");
  const auto request = makeTextureRequest(fixturePath("tiny_rgba.png"), dir.path / "out", "checker", "srgb");

  CHECK(runCookCommand(request) == 0);
  CHECK(fs::exists(dir.path / "out" / "checker.atex"));
  CHECK(fs::exists(dir.path / "out" / "checker.atex.meta.txt"));
  CHECK(fs::exists(dir.path / "out" / "checker.stamp"));

  const auto metadata =
      atlantis::asset_system::parseTextureMetadata(readFileText(dir.path / "out" / "checker.atex.meta.txt"));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().width == 2);
  CHECK(metadata.value().height == 2);
  CHECK(metadata.value().format == atlantis::asset_system::TextureColorSpace::Srgb);
  CHECK(metadata.value().channelsInFile == 4);
}

TEST_CASE("runCookCommand records the real decoded channel count for an RGB (no-alpha) source, and does not "
          "reject it",
          "[asset_cooker][texture]") {
  TempDirGuard dir("rgb_success");
  const auto request = makeTextureRequest(fixturePath("tiny_rgb.png"), dir.path / "out", "checker_rgb", "unorm");

  CHECK(runCookCommand(request) == 0);
  const auto metadata =
      atlantis::asset_system::parseTextureMetadata(readFileText(dir.path / "out" / "checker_rgb.atex.meta.txt"));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().channelsInFile == 3);

  // stbi_load() with desired_channels=4 always produces RGBA8 pixel
  // data regardless of the source's own real channel count -- the
  // artifact itself is still a normal, decodable 4-channel texture.
  const auto artifactBytes = readFileText(dir.path / "out" / "checker_rgb.atex");
  std::vector<std::byte> bytes(artifactBytes.size());
  for (std::size_t i = 0; i < artifactBytes.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(artifactBytes[i]));
  }
  const auto decoded = atlantis::asset_system::decodeTextureArtifact(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().width == 2);
  CHECK(decoded.value().height == 2);
}

TEST_CASE("runCookCommand records the real decoded channel count for a grayscale source, and does not reject it",
          "[asset_cooker][texture]") {
  TempDirGuard dir("grayscale_success");
  const auto request =
      makeTextureRequest(fixturePath("tiny_grayscale.png"), dir.path / "out", "checker_gray", "unorm");

  CHECK(runCookCommand(request) == 0);
  const auto metadata =
      atlantis::asset_system::parseTextureMetadata(readFileText(dir.path / "out" / "checker_gray.atex.meta.txt"));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().channelsInFile == 1);
}

TEST_CASE("runCookCommand fails cleanly on a corrupted PNG source", "[asset_cooker][texture]") {
  TempDirGuard dir("corrupted_source");
  const auto request = makeTextureRequest(fixturePath("corrupted.png"), dir.path / "out", "bad", "unorm");

  CHECK(runCookCommand(request) != 0);
  CHECK_FALSE(fs::exists(dir.path / "out" / "bad.atex"));
  CHECK_FALSE(fs::exists(dir.path / "out" / "bad.stamp"));
}

TEST_CASE("runCookCommand fails cleanly when the PNG source file does not exist", "[asset_cooker][texture]") {
  TempDirGuard dir("missing_source");
  const auto request =
      makeTextureRequest(fixturePath("does_not_exist.png"), dir.path / "out", "missing", "unorm");

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand rejects an unrecognized --color-space value", "[asset_cooker][texture]") {
  TempDirGuard dir("bad_color_space");
  const auto request = makeTextureRequest(fixturePath("tiny_rgba.png"), dir.path / "out", "checker", "linear");

  CHECK(runCookCommand(request) != 0);
  CHECK_FALSE(fs::exists(dir.path / "out" / "checker.atex"));
}

TEST_CASE("runCookCommand cooking the same source PNG twice under two names/color spaces still shares one AssetId "
          "-- NAME only disambiguates build-output file naming, never asset identity",
          "[asset_cooker][texture]") {
  // Plan 0016's own "Human Review Correction -- 2026-08-24": this is no
  // longer the textured fixture's own mechanism (the fixture now uses
  // two independent, byte-identical source PNGs with two distinct
  // logical paths instead -- see assets/CMakeLists.txt). This case is
  // kept as a regression test for cookTexture()'s own correct, narrower
  // behavior: NAME only disambiguates where the cooker writes its
  // output on disk (BYPRODUCTS collision avoidance); it was never an
  // identity mechanism, and computeAssetId() is still, correctly, a
  // pure function of the normalized SOURCE-derived logical path alone
  // -- two cooks of the same real SOURCE necessarily still share one
  // AssetId. The bug the Correction fixed was one layer up, in
  // atlantis_add_texture_asset()'s own now-removed collision-detector
  // bypass that let the CMake declaration layer register two named
  // assets against one shared SOURCE as if they were legitimately
  // distinct assets.
  TempDirGuard dir("cooked_twice");
  const fs::path source = fixturePath("tiny_rgba.png");
  const auto unormRequest = makeTextureRequest(source, dir.path / "out", "checker_unorm", "unorm");
  const auto srgbRequest = makeTextureRequest(source, dir.path / "out", "checker_srgb", "srgb");

  CHECK(runCookCommand(unormRequest) == 0);
  CHECK(runCookCommand(srgbRequest) == 0);

  CHECK(fs::exists(dir.path / "out" / "checker_unorm.atex"));
  CHECK(fs::exists(dir.path / "out" / "checker_srgb.atex"));

  const auto unormMetadata = atlantis::asset_system::parseTextureMetadata(
      readFileText(dir.path / "out" / "checker_unorm.atex.meta.txt"));
  const auto srgbMetadata =
      atlantis::asset_system::parseTextureMetadata(readFileText(dir.path / "out" / "checker_srgb.atex.meta.txt"));
  REQUIRE(unormMetadata.isOk());
  REQUIRE(srgbMetadata.isOk());
  CHECK(unormMetadata.value().format == atlantis::asset_system::TextureColorSpace::Unorm);
  CHECK(srgbMetadata.value().format == atlantis::asset_system::TextureColorSpace::Srgb);
  // Both cooked from the same source -- same logical path, same AssetId.
  CHECK(unormMetadata.value().assetId == srgbMetadata.value().assetId);
  CHECK(unormMetadata.value().sourceLogicalPath == srgbMetadata.value().sourceLogicalPath);
}

// Spec 0045: a DDS source cooks its whole mip chain, verbatim, into .atex v3.
TEST_CASE("runCookCommand cooks the string-lights DDS with all 9 levels, byte for byte", "[asset_cooker][texture]") {
  TempDirGuard dir("dds_chain");
  const fs::path source = ATLANTIS_STRINGLIGHTS_DDS_PATH;
  const auto request = makeTextureRequest(source, dir.path / "out", "stringlights", "unorm");
  REQUIRE(runCookCommand(request) == 0);

  const std::string ddsText = readFileText(source);
  REQUIRE(ddsText.size() == 43876);
  const std::string artifactText = readFileText(dir.path / "out" / "stringlights.atex");
  std::vector<std::byte> bytes(artifactText.size());
  for (std::size_t i = 0; i < artifactText.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(artifactText[i]));
  }
  const auto decoded = atlantis::asset_system::decodeTextureArtifact(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().mipCount == 9);
  REQUIRE(decoded.value().pixelBytes.size() == 43728);
  for (std::size_t i = 0; i < decoded.value().pixelBytes.size(); ++i) {
    if (decoded.value().pixelBytes[i] != static_cast<std::uint8_t>(ddsText[148 + i])) {
      FAIL("chain byte " << i << " differs from the DDS payload");
    }
  }
  const auto metadata =
      atlantis::asset_system::parseTextureMetadata(readFileText(dir.path / "out" / "stringlights.atex.meta.txt"));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().mipCount == 9);
}

TEST_CASE("runCookCommand writes mip_count 1 for a PNG source (no mip is generated)", "[asset_cooker][texture]") {
  TempDirGuard dir("png_single_level");
  const auto request = makeTextureRequest(fixturePath("tiny_rgba.png"), dir.path / "out", "checker", "srgb");
  REQUIRE(runCookCommand(request) == 0);
  const auto metadata =
      atlantis::asset_system::parseTextureMetadata(readFileText(dir.path / "out" / "checker.atex.meta.txt"));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().mipCount == 1);
}
