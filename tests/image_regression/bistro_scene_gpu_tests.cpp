#include "bloom_test_config.h"
#include "fixture/emissive_demo_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"
#include "support/png_codec.h"
#include "support/provenance.h"

#include <atlantis/asset_system/logical_path.h>
#include <atlantis/renderer/bloom.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/world/camera.h>
#include <atlantis/world/light.h>
#include <atlantis/world/world.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

// Plan 0046 Milestone 3 (Spec 0046 R1/R3, Plan 0046 M3 step 4): the assembled
// Bistro scene -- the build step's own cooked set, dependency manifest and
// overlay -- loads through the Runtime's scene path, resolves every
// renderable, and renders a frame, with no environment (ruling O1) and the
// overlay camera's own fog and bloom. Content-gated: SKIPs when the Bistro
// build step was not declared (no content at configure time). Also the
// milestone's measurement point: load time, first frame, steady frame and
// process peak memory, reported with WARN.

namespace fs = std::filesystem;
using atlantis::image_regression::addBloomShaderPaths;
using atlantis::image_regression::BloomTestSceneFiles;
using atlantis::image_regression::buildDarkEmissiveConfig;
using atlantis::image_regression::EmissiveDemoFixture;
using atlantis::image_regression::PixelBuffer;

namespace {

std::vector<std::string> nonEmptyLines(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<std::string> out;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) out.push_back(line);
  }
  return out;
}

struct PeakMemory {
  double workingSetGiB = 0.0;
  double commitGiB = 0.0;
  double physicalGiB = 0.0;
};

PeakMemory processPeakMemory() {
  PeakMemory peak;
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) {
    peak.workingSetGiB = static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0 * 1024.0);
    peak.commitGiB = static_cast<double>(counters.PeakPagefileUsage) / (1024.0 * 1024.0 * 1024.0);
  }
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof status;
  if (GlobalMemoryStatusEx(&status)) {
    peak.physicalGiB = static_cast<double>(status.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
  }
#endif
  return peak;
}

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

TEST_CASE("The assembled Bistro scene validates, loads through the Runtime path, resolves every renderable and "
          "renders a frame lit by its own overlay, with no environment",
          "[bistro]") {
#if !defined(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)
  SKIP("No Bistro build step: content/bistro was absent at configure time -- run tools/content/fetch_bistro.ps1 "
       "and re-configure");
#else
  const fs::path importDir{ATLANTIS_BISTRO_SCENE_IMPORT_DIR};
  if (!fs::exists(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)) SKIP("Bistro build step has not run");

  // 1. The whole declared set passes the cooker's own --validate-set, and
  //    the dependency manifest lists each of the 1070 assets once.
  const fs::path assetList = importDir / "asset_list.txt";
  CHECK(nonEmptyLines(assetList).size() == 551 + 254 + 265);
  const std::string validate = "\"\"" + std::string(ATLANTIS_ASSET_COOKER_EXECUTABLE) + "\" --validate-set " +
                               "\"--asset-list=" + assetList.generic_string() + "\"\"";
  REQUIRE(std::system(validate.c_str()) == 0);
  const std::vector<std::string> manifest = nonEmptyLines(ATLANTIS_BISTRO_SCENE_MANIFEST_PATH);
  CHECK(manifest.size() == 1070);

  // 2. Load (Phase 1: every mesh, material and texture read from disk).
  const BloomTestSceneFiles scene{ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH, ATLANTIS_BISTRO_SCENE_METADATA_PATH,
                                  ATLANTIS_BISTRO_SCENE_MANIFEST_PATH};
  auto config = buildDarkEmissiveConfig(scene);  // no environment (ruling O1)
  addBloomShaderPaths(config);
  const auto loadStart = std::chrono::steady_clock::now();
  auto fixtureResult = atlantis::image_regression::setUpEmissiveDemoFixture(
      config, ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH, ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  EmissiveDemoFixture fixture = std::move(fixtureResult.value());
  const double loadSeconds = secondsSince(loadStart);

  // The overlay's camera is active, with its fog and bloom.
  REQUIRE(fixture.world.has_value());
  const auto activeCamera = fixture.world->activeCamera();
  REQUIRE(activeCamera.has_value());
  const auto camera = fixture.world->getCamera(*activeCamera);
  REQUIRE(camera.isOk());
  CHECK(camera.value().fog.density > 0.0f);
  CHECK(camera.value().bloom.strength > 0.0f);
  CHECK_FALSE(fixture.environmentData.has_value());

  // 3. The first frame realizes and uploads every material and texture.
  const auto bloom =
      atlantis::image_regression::makeFixtureBloomInput(fixture, camera.value().bloom.strength,
                                                        camera.value().bloom.threshold);
  const auto firstStart = std::chrono::steady_clock::now();
  auto first = atlantis::image_regression::renderEmissiveDemoFrame(fixture, true, false, &bloom);
  const double firstFrameSeconds = secondsSince(firstStart);
  INFO("PbrNormalMapDemoRenderError " << (first.isErr() ? static_cast<int>(first.error()) : -1));
  REQUIRE(first.isOk());
  // Every renderable resolves (mesh and material), but 368 of the 2909 sit
  // under a non-uniformly-scaled node chain, and the Runtime's existing
  // conformal-transform rule (checkConformalTransform(), PbrDirectLit)
  // skips those per frame -- the fixture applies the same rule. Pinned
  // here as measured (Plan 0046 M3 finding), not endorsed.
  CHECK(fixture.lastDrawItemCount == 2909 - 368);
  CHECK(fixture.materialResourceMap.size() == 254);

  // 4. Steady state: every resource already realized.
  constexpr int kSteadyFrames = 10;
  const auto steadyStart = std::chrono::steady_clock::now();
  PixelBuffer last;
  for (int i = 0; i < kSteadyFrames; ++i) {
    auto frame = atlantis::image_regression::renderEmissiveDemoFrame(fixture, true, false, &bloom);
    REQUIRE(frame.isOk());
    last = std::move(frame.value());
  }
  const double steadyFrameMs = secondsSince(steadyStart) * 1000.0 / kSteadyFrames;
  CHECK(last.rgba8 == first.value().rgba8);  // deterministic once realized

  // A lit, non-degenerate frame: not all one value.
  std::size_t distinct = 0;
  for (std::size_t i = 4; i < last.rgba8.size(); i += 4) distinct += last.rgba8[i] != last.rgba8[0] ? 1 : 0;
  CHECK(distinct > last.rgba8.size() / 8);

  // For the human look review (Plan 0046 M4): the fixture's own frame.
  const fs::path review = fs::path(ATLANTIS_BISTRO_REVIEW_OUTPUT_DIR) / "bistro_fixture_512x512.png";
  fs::create_directories(review.parent_path());
  CHECK(atlantis::image_regression::encodePng(review, last).isOk());

  // 5. Memory (Plan 0046 P13 / ruling O4): gated on the load succeeding at
  //    all and the peak commit staying under half the machine's memory; the
  //    Spec's estimate (~4.4 GiB at the realization peak) is reported beside it.
  const PeakMemory peak = processPeakMemory();
  WARN("Bistro: load " << loadSeconds << " s; first frame (realize + upload + render) " << firstFrameSeconds
                       << " s; steady frame " << steadyFrameMs << " ms (fixture, 512x512, " << kSteadyFrames
                       << "-frame mean incl. readback + waitIdle); peak working set " << peak.workingSetGiB
                       << " GiB, peak commit " << peak.commitGiB << " GiB of " << peak.physicalGiB
                       << " GiB physical (Spec estimate ~4.4 GiB)");
  if (peak.physicalGiB > 0.0) CHECK(peak.commitGiB < peak.physicalGiB * 0.5);
#endif
}

// ---------------------------------------------------------------------------
// Plan 0046 Milestone 5 (Spec 0046 R6, ADR-0095, Plan 0046 P12): the
// bistro_demo golden -- the frame atlantis_image_regression_bistro_demo_
// golden_generator captures (the overlay camera, its fog and bloom, no
// environment), compared at zero tolerance (ADR-0042), with three
// discriminators that must fail: fog off, bloom off, every point light off
// (each a World edit on the loaded scene). Content-gated like the test
// above; the sidecar's content pin (schema 3) must match the fetch script.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kBistroGolden = "bistro_demo/bistro_demo_512x512_rgba8unorm";
constexpr const char* kBistroGoldenSlug = "bistro_demo_512x512_rgba8unorm";

enum class BistroVariant { Golden, FogOff, BloomOff, PointLightsOff };

#if defined(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)
[[nodiscard]] atlantis::image_regression::ComparisonReport renderBistroAndCompare(BistroVariant variant) {
  const BloomTestSceneFiles scene{ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH, ATLANTIS_BISTRO_SCENE_METADATA_PATH,
                                  ATLANTIS_BISTRO_SCENE_MANIFEST_PATH};
  auto config = buildDarkEmissiveConfig(scene);
  addBloomShaderPaths(config);
  auto fixtureResult = atlantis::image_regression::setUpEmissiveDemoFixture(
      config, ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH, ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  EmissiveDemoFixture fixture = std::move(fixtureResult.value());
  atlantis::world::World& world = *fixture.world;
  const auto cameraId = world.activeCamera();
  REQUIRE(cameraId.has_value());
  atlantis::world::Camera camera = world.getCamera(*cameraId).value();
  if (variant == BistroVariant::FogOff) camera.fog.density = 0.0f;
  if (variant == BistroVariant::BloomOff) camera.bloom.strength = 0.0f;
  REQUIRE(world.setCamera(*cameraId, camera).isOk());
  if (variant == BistroVariant::PointLightsOff) {
    std::size_t pointLights = 0;
    for (const auto& id : world.lightEntities()) {
      atlantis::world::Light light = world.getLight(id).value();
      if (light.kind != atlantis::world::LightKind::Point) continue;
      light.intensity = 0.0f;
      REQUIRE(world.setLight(id, light).isOk());
      ++pointLights;
    }
    REQUIRE(pointLights == 59);
  }

  // Exactly the generator's call: no explicit BloomInput, the camera's
  // bloom= group drives the chain.
  auto frame = atlantis::image_regression::renderEmissiveDemoFrame(fixture);
  REQUIRE(frame.isOk());
  REQUIRE(fixture.device->waitIdle().isOk());
  const PixelBuffer actual = std::move(frame.value());

  const fs::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldensDir / (std::string(kBistroGolden) + ".png"),
                                                                  goldensDir /
                                                                      (std::string(kBistroGolden) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed " << kBistroGolden << " golden must load and validate cleanly");
    REQUIRE(golden.isOk());
  }
  REQUIRE(actual.width == golden.value().pixels.width);
  REQUIRE(actual.height == golden.value().pixels.height);
  const auto report = atlantis::image_regression::compareBuffers(actual, golden.value().pixels);
  if (!report.passed && variant == BistroVariant::Golden) {
    (void)atlantis::image_regression::writeFailureArtifacts(ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, kBistroGoldenSlug,
                                                            actual, golden.value().pixels);
  }
  return report;
}
#endif

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed bistro_demo golden passes, and its sidecar's content "
          "pin matches the fetch script (ADR-0095)",
          "[bistro]") {
#if !defined(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)
  SKIP("No Bistro build step: content/bistro was absent at configure time");
#else
  if (!fs::exists(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)) SKIP("Bistro build step has not run");
  const fs::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  fs::remove(outputDir / (std::string(kBistroGoldenSlug) + "_actual.png"));
  fs::remove(outputDir / (std::string(kBistroGoldenSlug) + "_diff.png"));

  std::ifstream sidecarFile(fs::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) /
                                (std::string(kBistroGolden) + ".sidecar.txt"),
                            std::ios::binary);
  const std::string sidecarText((std::istreambuf_iterator<char>(sidecarFile)), std::istreambuf_iterator<char>());
  const auto provenance = atlantis::image_regression::parseGoldenProvenance(sidecarText);
  REQUIRE(provenance.isOk());
  CHECK(sidecarText.rfind("schema_version: 3\n", 0) == 0);
  CHECK(provenance.value().contentFetchScriptSha256 == ATLANTIS_BISTRO_FETCH_SCRIPT_SHA256);
  CHECK(provenance.value().contentSourceCommit.size() == 40);

  CHECK(renderBistroAndCompare(BistroVariant::Golden).passed);
#endif
}

TEST_CASE("The bistro_demo frame with fog off fails comparison against the real bistro_demo golden", "[bistro]") {
#if !defined(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)
  SKIP("No Bistro build step: content/bistro was absent at configure time");
#else
  if (!fs::exists(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)) SKIP("Bistro build step has not run");
  CHECK_FALSE(renderBistroAndCompare(BistroVariant::FogOff).passed);
#endif
}

TEST_CASE("The bistro_demo frame with bloom off fails comparison against the real bistro_demo golden", "[bistro]") {
#if !defined(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)
  SKIP("No Bistro build step: content/bistro was absent at configure time");
#else
  if (!fs::exists(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)) SKIP("Bistro build step has not run");
  CHECK_FALSE(renderBistroAndCompare(BistroVariant::BloomOff).passed);
#endif
}

TEST_CASE("The bistro_demo frame with every point light off fails comparison against the real bistro_demo golden",
          "[bistro]") {
#if !defined(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)
  SKIP("No Bistro build step: content/bistro was absent at configure time");
#else
  if (!fs::exists(ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH)) SKIP("Bistro build step has not run");
  CHECK_FALSE(renderBistroAndCompare(BistroVariant::PointLightsOff).passed);
#endif
}
