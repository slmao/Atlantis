#include "bloom_test_config.h"
#include "fixture/emissive_demo_fixture.h"
#include "support/emissive_differential.h"
#include "support/fog_differential.h"
#include "support/pixel_diff.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/renderer/bloom.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/world/camera.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Plan 0044 Milestone 2 (Spec 0044 R1-R3, R7; ADR-0092): bloom switched
// on, on the dark emissive fixture (PbrNormalMapDemoFixture, ruling O4):
// - neutrality: a knee above every channel renders bloom-on byte-
//   identically to bloom-off (R7);
// - halo: each emissive_demo sphere rendered alone -- orange (4, 0.5, 0)
//   and blue (0.2, 0.3, 2.5) are above the 1.0 knee and grow a halo
//   outside their silhouettes; green (0.1, 1.0, 0.25) sits exactly at the
//   knee, so its bright-pass is 0 and bloom adds nothing; with every
//   emissive zeroed (the black control alone) bloom adds nothing either.
//   In the full scene the halos overlap (the six-level chain reaches
//   ~100+ px), so the at-knee property is checked with the sphere alone;
// - halo shape: radially monotone around an isolated source, growing
//   with strength;
// - fog feeds bloom: in bloom_fog_demo the HDR fog colour (3, 2.4, 1.5)
//   lifts the fogged backdrop above the knee, so the whole backdrop
//   glows with fog on and does not with fog off;
// - the drawFrame() gates (Plan 0044 P5): a bad strength, a bad threshold
//   and a mismatched target extent each report exactly one check failure.
// The two goldens (bloom_demo, bloom_fog_demo) and their discriminators
// land with the goldens themselves, at the end of this file.

using atlantis::image_regression::activeCameraFog;
using atlantis::image_regression::addBloomShaderPaths;
using atlantis::image_regression::BloomTestSceneFiles;
using atlantis::image_regression::buildDarkEmissiveConfig;
using atlantis::image_regression::EmissiveDemoFixture;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::projectMaterialSphere;
using atlantis::image_regression::renderEmissiveDemoFrame;
using atlantis::image_regression::setActiveCameraFog;
using atlantis::image_regression::setUpEmissiveDemoFixture;
using atlantis::renderer::BloomInput;

namespace {

constexpr BloomTestSceneFiles kEmissiveDemoScene{ATLANTIS_emissive_demo_scene_ARTIFACT_PATH,
                                                 ATLANTIS_emissive_demo_scene_METADATA_PATH,
                                                 ATLANTIS_emissive_demo_scene_MANIFEST_PATH};
constexpr BloomTestSceneFiles kBloomFogDemoScene{ATLANTIS_bloom_fog_demo_scene_ARTIFACT_PATH,
                                                 ATLANTIS_bloom_fog_demo_scene_METADATA_PATH,
                                                 ATLANTIS_bloom_fog_demo_scene_MANIFEST_PATH};

constexpr const char* kOrange = "materials/emissive_demo_orange.material.txt";
constexpr const char* kGreen = "materials/emissive_demo_green.material.txt";
constexpr const char* kBlue = "materials/emissive_demo_blue.material.txt";

[[nodiscard]] EmissiveDemoFixture setUpBloomFixture(const BloomTestSceneFiles& scene) {
  auto config = buildDarkEmissiveConfig(scene);
  addBloomShaderPaths(config);
  auto fixtureResult = setUpEmissiveDemoFixture(config, ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                                ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  REQUIRE(fixtureResult.value().bloomTargets.has_value());
  return std::move(fixtureResult.value());
}

// A BloomInput over the fixture's own resources.
[[nodiscard]] BloomInput makeBloomInput(EmissiveDemoFixture& fixture, float strength, float threshold) {
  return atlantis::image_regression::makeFixtureBloomInput(fixture, strength, threshold);
}

[[nodiscard]] PixelBuffer render(EmissiveDemoFixture& fixture, const BloomInput* bloom = nullptr) {
  auto result = renderEmissiveDemoFrame(fixture, true, false, bloom);
  REQUIRE(result.isOk());
  return std::move(result.value());
}

// Zeroes every scene material's emissiveFactor except keptMaterialPath's
// (nullptr: zero them all). Must run before the fixture's first render,
// which realizes the materials.
void keepOnlyEmissive(EmissiveDemoFixture& fixture, const char* keptMaterialPath) {
  const std::optional<atlantis::asset_system::AssetId> kept =
      keptMaterialPath != nullptr ? std::optional(atlantis::asset_system::computeAssetId(keptMaterialPath))
                                  : std::nullopt;
  for (auto& [id, material] : fixture.materialDataMap) {
    if (kept.has_value() && id == *kept) continue;
    for (float& component : material.emissiveFactor) component = 0.0f;
  }
}

void setActiveCameraBloomStrength(EmissiveDemoFixture& fixture, float strength) {
  auto& world = *fixture.world;
  const auto activeCamera = world.activeCamera();
  REQUIRE(activeCamera.has_value());
  auto camera = world.getCamera(*activeCamera);
  REQUIRE(camera.isOk());
  atlantis::world::Camera updated = camera.value();
  updated.bloom.strength = strength;
  REQUIRE(world.setCamera(*activeCamera, updated).isOk());
}

[[nodiscard]] bool framesIdentical(const PixelBuffer& a, const PixelBuffer& b) {
  return a.width == b.width && a.height == b.height && a.rgba8 == b.rgba8;
}

// Sum over RGB of on - off at (x, y): how much bloom brightened the pixel.
[[nodiscard]] int brightening(const PixelBuffer& on, const PixelBuffer& off, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * on.width + x) * 4;
  int sum = 0;
  for (std::size_t c = 0; c < 3; ++c) sum += static_cast<int>(on.rgba8[offset + c]) - off.rgba8[offset + c];
  return sum;
}

// Pixels in [x0, x1) x [y0, y1) whose RGBA differ between a and b.
[[nodiscard]] std::size_t countChanged(const PixelBuffer& a, const PixelBuffer& b, std::uint32_t x0, std::uint32_t x1,
                                       std::uint32_t y0, std::uint32_t y1) {
  std::size_t changed = 0;
  for (std::uint32_t y = y0; y < y1; ++y) {
    for (std::uint32_t x = x0; x < x1; ++x) {
      const std::size_t offset = (static_cast<std::size_t>(y) * a.width + x) * 4;
      for (std::size_t c = 0; c < 4; ++c) {
        if (a.rgba8[offset + c] != b.rgba8[offset + c]) {
          ++changed;
          break;
        }
      }
    }
  }
  return changed;
}

// The smallest and largest brightening() over [x0, x1) x [y0, y1).
[[nodiscard]] std::pair<int, int> brighteningRange(const PixelBuffer& on, const PixelBuffer& off, std::uint32_t x0,
                                                   std::uint32_t x1, std::uint32_t y0, std::uint32_t y1) {
  int lowest = brightening(on, off, x0, y0);
  int highest = lowest;
  for (std::uint32_t y = y0; y < y1; ++y) {
    for (std::uint32_t x = x0; x < x1; ++x) {
      const int value = brightening(on, off, x, y);
      lowest = std::min(lowest, value);
      highest = std::max(highest, value);
    }
  }
  return {lowest, highest};
}

struct IsolatedSphereResult {
  std::size_t changedPixels = 0;  // anywhere in the frame
  int ringBrightening = 0;        // just above the sphere's silhouette, if a sphere was kept
};

// emissive_demo with only keptMaterialPath emissive, rendered bloom off and
// bloom on (strength 0.8, knee 1.0).
[[nodiscard]] IsolatedSphereResult renderIsolatedSphere(const char* keptMaterialPath) {
  EmissiveDemoFixture fixture = setUpBloomFixture(kEmissiveDemoScene);
  keepOnlyEmissive(fixture, keptMaterialPath);
  const PixelBuffer off = render(fixture);
  const BloomInput bloom = makeBloomInput(fixture, 0.8f, 1.0f);
  const PixelBuffer on = render(fixture, &bloom);
  REQUIRE(fixture.device->waitIdle().isOk());

  IsolatedSphereResult result;
  result.changedPixels = countChanged(on, off, 0, on.width, 0, on.height);
  if (keptMaterialPath != nullptr) {
    const auto circle =
        projectMaterialSphere(fixture, atlantis::asset_system::computeAssetId(keptMaterialPath), 1.0f, on.width);
    REQUIRE(circle.has_value());
    const auto x = static_cast<std::uint32_t>(circle->centerX);
    const auto y = static_cast<std::uint32_t>(circle->centerY - circle->radius - 8.0f);
    result.ringBrightening = brightening(on, off, x, y);
  }
  return result;
}

class ScopedFailureCapture {
 public:
  explicit ScopedFailureCapture(std::vector<std::string>& failures)
      : previous_(atlantis::assertions::setFailureHandler(
            [&failures](const atlantis::AssertFailureInfo& info) { failures.emplace_back(info.message); })) {}
  ~ScopedFailureCapture() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureCapture(const ScopedFailureCapture&) = delete;
  ScopedFailureCapture& operator=(const ScopedFailureCapture&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

}  // namespace

TEST_CASE("Bloom neutrality: a knee above the brightest channel renders bloom-on byte-identically to bloom-off",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUpBloomFixture(kEmissiveDemoScene);
  // emissive_demo's brightest channel is orange's 4.0; at a knee of 10 the
  // D1 bright-pass is exactly 0 everywhere, so the composite is hdr + 0.
  const BloomInput bloom = makeBloomInput(fixture, 0.8f, 10.0f);
  const PixelBuffer on = render(fixture, &bloom);
  const PixelBuffer off = render(fixture);
  CHECK(framesIdentical(on, off));
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Bloom halo: orange and blue alone grow a halo; green at the knee and the black control add nothing",
          "[image_regression][gpu][bloom]") {
  const IsolatedSphereResult orange = renderIsolatedSphere(kOrange);
  const IsolatedSphereResult blue = renderIsolatedSphere(kBlue);
  const IsolatedSphereResult green = renderIsolatedSphere(kGreen);
  const IsolatedSphereResult control = renderIsolatedSphere(nullptr);

  // Above the knee: the frame changes and the ring just outside the
  // silhouette is brighter with bloom than without.
  CHECK(orange.changedPixels > 0);
  CHECK(orange.ringBrightening > 0);
  CHECK(blue.changedPixels > 0);
  CHECK(blue.ringBrightening > 0);
  // Green's brightest channel is exactly 1.0: max(1 - 1, 0) = 0, so the
  // whole frame is byte-identical to bloom off.
  CHECK(green.changedPixels == 0);
  // Every emissive zeroed: a black frame, and bloom adds nothing.
  CHECK(control.changedPixels == 0);
}

TEST_CASE("Bloom halo shape: an isolated source's halo falls off monotonically and grows with strength",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUpBloomFixture(kEmissiveDemoScene);
  keepOnlyEmissive(fixture, kOrange);
  const PixelBuffer off = render(fixture);
  const BloomInput full = makeBloomInput(fixture, 0.8f, 1.0f);
  const PixelBuffer on = render(fixture, &full);
  const BloomInput half = makeBloomInput(fixture, 0.4f, 1.0f);
  const PixelBuffer halfOn = render(fixture, &half);

  const auto circle = projectMaterialSphere(fixture, atlantis::asset_system::computeAssetId(kOrange), 1.0f, on.width);
  REQUIRE(circle.has_value());
  // Straight up from the orange sphere's top edge: nothing else is there.
  const auto x = static_cast<std::uint32_t>(circle->centerX);
  const float top = circle->centerY - circle->radius;
  const std::array<std::uint32_t, 4> ys = {static_cast<std::uint32_t>(top - 8.0f),
                                           static_cast<std::uint32_t>(top - 24.0f),
                                           static_cast<std::uint32_t>(top - 48.0f),
                                           static_cast<std::uint32_t>(top - 80.0f)};
  int previous = brightening(on, off, x, ys[0]);
  CHECK(previous > 0);
  for (std::size_t i = 1; i < ys.size(); ++i) {
    const int current = brightening(on, off, x, ys[i]);
    INFO("y " << ys[i] << ": " << current << " (previous " << previous << ")");
    CHECK(current <= previous);
    CHECK(current >= 0);
    previous = current;
  }
  // Half the strength, a dimmer halo at the same point.
  CHECK(brightening(halfOn, off, x, ys[0]) < brightening(on, off, x, ys[0]));
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Fog feeds bloom: bloom_fog_demo's HDR fog lifts the backdrop above the knee, and it glows only with fog on",
          "[image_regression][gpu][bloom][fog]") {
  EmissiveDemoFixture fixture = setUpBloomFixture(kBloomFogDemoScene);
  // The scene's camera turns bloom on (0.8, knee 1.0); the fixture follows it.
  const PixelBuffer fogOnBloomOn = render(fixture);
  setActiveCameraBloomStrength(fixture, 0.0f);
  const PixelBuffer fogOnBloomOff = render(fixture);

  atlantis::world::CameraFog fog = activeCameraFog(fixture);
  REQUIRE(fog.density > 0.0f);
  fog.density = 0.0f;
  setActiveCameraFog(fixture, fog);
  const PixelBuffer fogOffBloomOff = render(fixture);
  setActiveCameraBloomStrength(fixture, 0.8f);
  const PixelBuffer fogOffBloomOn = render(fixture);
  REQUIRE(fixture.device->waitIdle().isOk());

  // A 96 x 96 corner of the backdrop, far from the lamp at the centre.
  const std::size_t fogOnCorner = countChanged(fogOnBloomOn, fogOnBloomOff, 0, 96, 0, 96);
  const auto [fogOnLowest, fogOnHighest] = brighteningRange(fogOnBloomOn, fogOnBloomOff, 0, 96, 0, 96);
  const auto [fogOffLowest, fogOffHighest] = brighteningRange(fogOffBloomOn, fogOffBloomOff, 0, 96, 0, 96);
  INFO("corner brightening: fog on [" << fogOnLowest << ", " << fogOnHighest << "] over " << fogOnCorner
                                      << " changed pixels; fog off [" << fogOffLowest << ", " << fogOffHighest
                                      << "]");
  // Fog on: the fogged backdrop is above the knee, so bloom brightens every
  // corner pixel -- the glow lies outside the lamp.
  CHECK(fogOnCorner == 96u * 96u);
  // Fog off: the backdrop is black, and only the far tail of the lamp's own
  // halo reaches the corner (the 8 x 8 bottom level spans the frame) --
  // fainter everywhere than the fog's glow at its faintest.
  CHECK(fogOffLowest >= 0);
  CHECK(fogOffHighest < fogOnLowest);
  // And the halos differ: fog changes what bloom adds.
  CHECK_FALSE(framesIdentical(fogOnBloomOn, fogOffBloomOn));
}

TEST_CASE("drawFrame() bloom gates: a bad strength, a bad threshold and a mismatched extent each report one failure",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUpBloomFixture(kEmissiveDemoScene);
  std::vector<std::string> failures;
  {
    ScopedFailureCapture capture(failures);
    const BloomInput tooStrong = makeBloomInput(fixture, 1.5f, 1.0f);
    (void)render(fixture, &tooStrong);
  }
  REQUIRE(failures.size() == 1);
  CHECK(failures[0] == "BloomInput::strength must be finite and in [0, 1]");

  failures.clear();
  {
    ScopedFailureCapture capture(failures);
    const BloomInput negativeKnee = makeBloomInput(fixture, 0.8f, -1.0f);
    (void)render(fixture, &negativeKnee);
  }
  REQUIRE(failures.size() == 1);
  CHECK(failures[0] == "BloomInput::threshold must be finite and >= 0");

  failures.clear();
  auto smallTargets = atlantis::renderer::createBloomTargets(*fixture.device, {256, 256});
  REQUIRE(smallTargets.isOk());
  {
    ScopedFailureCapture capture(failures);
    BloomInput mismatched{.targets = smallTargets.value(), .strength = 0.8f, .threshold = 1.0f};
    mismatched.pipelines = makeBloomInput(fixture, 0.8f, 1.0f).pipelines;
    (void)render(fixture, &mismatched);
  }
  REQUIRE(failures.size() == 1);
  CHECK(failures[0] == "BloomInput::targets' extent must equal the HdrColorTarget's extent");
  REQUIRE(fixture.device->waitIdle().isOk());
}
