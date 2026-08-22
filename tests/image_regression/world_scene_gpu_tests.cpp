#include "fixture/world_scene_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <filesystem>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

// Plan 0014 Section D10: a new, additional TEST_CASE in the existing
// atlantis_image_regression_gpu_tests executable -- the existing
// minimal_cube golden and its own test case (image_regression_gpu_tests.cpp)
// are untouched.

using namespace atlantis::image_regression;

namespace {
constexpr const char* kWorldSceneGoldenName = "world_scene/world_scene_512x512_rgba8unorm";
constexpr const char* kWorldSceneGoldenSlug = "world_scene_512x512_rgba8unorm";

[[nodiscard]] std::filesystem::path goldenPngPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] std::filesystem::path goldenSidecarPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}
}  // namespace

TEST_CASE("Full capture-compare cycle against the committed world_scene golden passes",
          "[image_regression][gpu]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kWorldSceneGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kWorldSceneGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  const std::filesystem::path artifactPath =
      std::filesystem::path(ATLANTIS_ASSET_ARTIFACT_DIR) / "meshes" / "minimal_cube.amesh";
  const std::filesystem::path metadataPath =
      std::filesystem::path(ATLANTIS_ASSET_ARTIFACT_DIR) / "meshes" / "minimal_cube.amesh.meta.txt";

  auto fixtureResult = setUpWorldSceneFixture(artifactPath.string().c_str(), metadataPath.string().c_str());
  REQUIRE(fixtureResult.isOk());
  WorldSceneFixture fixture = std::move(fixtureResult.value());

  auto renderResult = renderOneWorldSceneFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer actual = std::move(renderResult.value());

  auto goldenResult =
      loadAndValidateGolden(goldenPngPath(kWorldSceneGoldenName), goldenSidecarPath(kWorldSceneGoldenName));
  {
    INFO("INVALID GOLDEN: the committed world_scene golden must load and validate cleanly -- run the "
         "atlantis_image_regression_world_scene_golden_generator tool if it has not yet been captured");
    REQUIRE(goldenResult.isOk());
  }
  const ValidatedGolden& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const ComparisonReport report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kWorldSceneGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}
