#include "bloom_test_config.h"
#include "fixture/emissive_demo_fixture.h"
#include "support/png_codec.h"

#include <atlantis/asset_system/logical_path.h>
#include <atlantis/renderer/bloom.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
