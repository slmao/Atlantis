#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/load_texture.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Plan 0016's own "Human Review Correction -- 2026-08-24" (Verification
// V45/V46/V49): the textured fixture's own two source PNGs
// (assets/textures/textured_quad_source_unorm.png,
// assets/textures/textured_quad_source_srgb.png, declared unconditionally
// in assets/CMakeLists.txt, same mechanism minimal_cube/world_scene
// already use) are proven, by this file, to be (a) byte-identical as
// checked-in source files, (b) cooked into two artifacts with distinct
// AssetIds derived from their own distinct normalized logical paths, and
// (c) independently loadable and distinguishable in the same process --
// the three properties the pre-correction design (one PNG cooked twice
// under a shared SOURCE) could not actually guarantee.

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string readFileBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

TEST_CASE("The textured fixture's two checked-in source PNGs are byte-identical", "[asset_system][texture_fixture]") {
  const std::string unormBytes = readFileBytes(ATLANTIS_TEXTURED_QUAD_SOURCE_UNORM_PATH);
  const std::string srgbBytes = readFileBytes(ATLANTIS_TEXTURED_QUAD_SOURCE_SRGB_PATH);

  REQUIRE_FALSE(unormBytes.empty());
  CHECK(unormBytes.size() == srgbBytes.size());
  CHECK(unormBytes == srgbBytes);
}

TEST_CASE("The textured fixture's two cooked texture artifacts have distinct AssetIds derived from their own "
          "distinct normalized logical paths",
          "[asset_system][texture_fixture]") {
  const auto unormMetadata =
      parseTextureMetadata(readFileBytes(ATLANTIS_textured_quad_unorm_METADATA_PATH));
  const auto srgbMetadata = parseTextureMetadata(readFileBytes(ATLANTIS_textured_quad_srgb_METADATA_PATH));
  REQUIRE(unormMetadata.isOk());
  REQUIRE(srgbMetadata.isOk());

  const auto expectedUnormPath = normalizeLogicalPath("textures/textured_quad_source_unorm.png");
  const auto expectedSrgbPath = normalizeLogicalPath("textures/textured_quad_source_srgb.png");
  REQUIRE(expectedUnormPath.isOk());
  REQUIRE(expectedSrgbPath.isOk());

  CHECK(unormMetadata.value().sourceLogicalPath == expectedUnormPath.value());
  CHECK(srgbMetadata.value().sourceLogicalPath == expectedSrgbPath.value());
  CHECK(unormMetadata.value().assetId == computeAssetId(expectedUnormPath.value()));
  CHECK(srgbMetadata.value().assetId == computeAssetId(expectedSrgbPath.value()));

  // The real fix this Correction restores: two genuinely different
  // artifacts (different color-space metadata) no longer share one
  // AssetId the way the pre-correction "same SOURCE, two NAMEs" design
  // silently did.
  CHECK(unormMetadata.value().assetId != srgbMetadata.value().assetId);
  CHECK(unormMetadata.value().sourceLogicalPath != srgbMetadata.value().sourceLogicalPath);
}

TEST_CASE("Both textured fixture assets load independently and remain distinguishable in the same process",
          "[asset_system][texture_fixture]") {
  const auto unormAsset =
      loadTextureAsset(ATLANTIS_textured_quad_unorm_ARTIFACT_PATH, ATLANTIS_textured_quad_unorm_METADATA_PATH);
  const auto srgbAsset =
      loadTextureAsset(ATLANTIS_textured_quad_srgb_ARTIFACT_PATH, ATLANTIS_textured_quad_srgb_METADATA_PATH);
  REQUIRE(unormAsset.isOk());
  REQUIRE(srgbAsset.isOk());

  // Cooked from byte-identical source pixels, so the decoded pixel data
  // itself is expected to match...
  CHECK(unormAsset.value().width == srgbAsset.value().width);
  CHECK(unormAsset.value().height == srgbAsset.value().height);
  CHECK(unormAsset.value().pixelBytes == srgbAsset.value().pixelBytes);
  // ...but each load resolved its own artifact/metadata pair correctly,
  // never conflating one asset's identity with the other's.
  CHECK(unormAsset.value().colorSpace == TextureColorSpace::Unorm);
  CHECK(srgbAsset.value().colorSpace == TextureColorSpace::Srgb);
}
