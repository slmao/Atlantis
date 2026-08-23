#include "fixture/world_scene_loaded_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <filesystem>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

// Plan 0015 Section D11/Step 9: a new, additional TEST_CASE in the
// existing atlantis_image_regression_gpu_tests executable, mirroring
// world_scene_gpu_tests.cpp exactly except for how the scene itself is
// built -- through the real cook -> decode -> resolve -> load ->
// instantiate pipeline (WorldSceneLoadedFixture) instead of
// WorldSceneFixture's own hand-authored World construction. Reuses the
// SAME, existing, UNMODIFIED world_scene golden -- no new golden is
// captured or committed by this file (Spec 0015's own explicit
// requirement); this test's own value is that the loaded scene
// reproduces the hand-authored one with zero pixel difference.

using namespace atlantis::image_regression;

namespace {
constexpr const char* kWorldSceneGoldenName = "world_scene/world_scene_512x512_rgba8unorm";
constexpr const char* kWorldSceneGoldenSlug = "world_scene_loaded_512x512_rgba8unorm";

[[nodiscard]] std::filesystem::path goldenPngPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] std::filesystem::path goldenSidecarPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}
}  // namespace

TEST_CASE("Full capture-compare cycle for the real, loaded world_scene asset against the existing, "
          "unmodified world_scene golden passes with zero difference",
          "[image_regression][gpu]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kWorldSceneGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kWorldSceneGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpWorldSceneLoadedFixture(ATLANTIS_WORLD_SCENE_ARTIFACT_PATH,
                                                      ATLANTIS_WORLD_SCENE_METADATA_PATH,
                                                      ATLANTIS_WORLD_SCENE_MANIFEST_PATH);
  REQUIRE(fixtureResult.isOk());
  WorldSceneLoadedFixture fixture = std::move(fixtureResult.value());

  auto renderResult = renderOneWorldSceneLoadedFrame(fixture);
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
