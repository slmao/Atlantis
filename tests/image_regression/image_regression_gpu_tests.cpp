#include "fixture/minimal_cube_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"
#include "support/provenance.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

// Every path this file's code uses comes from the three
// target_compile_definitions() macros this target's own CMakeLists.txt
// injects -- ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR,
// ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE,
// ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR -- never a path relative to this
// process's own working directory (Plan 0011 Sections 3.4/5.4).

using namespace atlantis::image_regression;

namespace {

constexpr const char* kMinimalCubeGoldenName = "minimal_cube/minimal_cube_512x512_rgba8unorm";
constexpr const char* kMinimalCubeGoldenSlug = "minimal_cube_512x512_rgba8unorm";

[[nodiscard]] std::filesystem::path goldenPngPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] std::filesystem::path goldenSidecarPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}

[[nodiscard]] std::string readFileToString(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed minimal_cube golden passes",
          "[image_regression][gpu]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kMinimalCubeGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kMinimalCubeGoldenSlug) + "_diff.png");
  // A stale diagnostic artifact from an already-fixed problem must never
  // linger and be mistaken for a current one.
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpMinimalCubeFixture();
  REQUIRE(fixtureResult.isOk());
  MinimalCubeFixture fixture = std::move(fixtureResult.value());

  auto renderResult = renderOneFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer actual = std::move(renderResult.value());

  auto goldenResult =
      loadAndValidateGolden(goldenPngPath(kMinimalCubeGoldenName), goldenSidecarPath(kMinimalCubeGoldenName));
  INFO("INVALID GOLDEN: the committed minimal_cube golden must load and validate cleanly");
  REQUIRE(goldenResult.isOk());
  const ValidatedGolden& validatedGolden = goldenResult.value();

  // Format/extent mismatch is a hard failure, distinct from an INVALID
  // GOLDEN outcome (which is about the golden's own internal
  // consistency, already ruled out above) -- never silently resized or
  // reformatted.
  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const ComparisonReport report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kMinimalCubeGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Repeated capture cycles against the same fixture produce byte-identical results",
          "[image_regression][gpu]") {
  auto fixtureResult = setUpMinimalCubeFixture();
  REQUIRE(fixtureResult.isOk());
  MinimalCubeFixture fixture = std::move(fixtureResult.value());

  constexpr int kCycleCount = 3;  // matches examples/headless_rendering_demo's own kCycleCount
  std::optional<PixelBuffer> firstCapture;
  for (int cycle = 0; cycle < kCycleCount; ++cycle) {
    auto renderResult = renderOneFrame(fixture);
    REQUIRE(renderResult.isOk());
    if (!firstCapture.has_value()) {
      firstCapture = std::move(renderResult.value());
    } else {
      REQUIRE(renderResult.value().rgba8 == firstCapture->rgba8);
    }
  }

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Loading a never-committed golden is reported as INVALID GOLDEN, not a crash or silent pass",
          "[image_regression][gpu]") {
  constexpr const char* kNeverCommittedGoldenName = "nonexistent_scene/nonexistent";
  const auto result =
      loadAndValidateGolden(goldenPngPath(kNeverCommittedGoldenName), goldenSidecarPath(kNeverCommittedGoldenName));

  INFO("INVALID GOLDEN: loadAndValidateGolden() for a never-committed golden must return Err(MissingPngFile), "
       "never Ok() and never a crash");
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::MissingPngFile);
}

TEST_CASE("Provenance mismatch is reported as a separate diagnostic, never merged into pass/fail",
          "[image_regression][gpu]") {
  if (!std::filesystem::exists(ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE)) {
    SKIP("current_environment.sidecar.txt is not populated on this machine -- copy "
         "tests/image_regression/current_environment.sidecar.txt.example and fill it in to exercise this case");
  }

  auto goldenResult =
      loadAndValidateGolden(goldenPngPath(kMinimalCubeGoldenName), goldenSidecarPath(kMinimalCubeGoldenName));
  REQUIRE(goldenResult.isOk());
  const Provenance& golden = goldenResult.value().provenance;

  auto environmentResult = parseEnvironmentProvenance(readFileToString(ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE));
  REQUIRE(environmentResult.isOk());
  EnvironmentProvenance current = environmentResult.value();
  current.driverVersion = current.driverVersion + "-deliberately-different";

  const auto diffs = compareProvenanceEnvironment(golden, current);
  REQUIRE(diffs.size() == 1);
  REQUIRE(diffs[0].fieldName == "driver_version");

  // Logging a mismatch never fails this TEST_CASE by itself -- only
  // compareBuffers()'s own passed value (exercised by the main
  // capture-compare TEST_CASE above) drives pass/fail. WARN() is
  // Catch2's own non-fatal diagnostic macro.
  WARN("PROVENANCE MISMATCH: driver_version differs (golden=" << golden.driverVersion
                                                                << ", current=" << current.driverVersion << ")");
}
