#include "fixture/lighting_demo_fixture.h"
#include "support/pixel_diff.h"

#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/light.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan 0040 Milestone 3 (Spec 0040 Req 7): the multi-light verification
// scene, assets/scenes/multi_light_demo.scene.txt -- eight Point lights,
// double the former four-light cap, rendered through the lit_textured
// path (one of the five shaders that iterate the point-light array, P4)
// by LightingDemoFixture, reused unchanged: that fixture is already
// parameterised by BootstrapConfig and allocates its camera buffer from
// sizeof(FrameLightingData), so only the scene paths differ.
//
// The composition is built so every contribution is individually
// identifiable without the golden: ground_plane shows the lit_textured_quad
// texture's 4x4 red/white checker; each light hangs 0.3 above the centre
// of one of the eight WHITE cells with range 1.2, so its pool (radius
// ~1.16 on the plane) stays inside its own 2.5-unit cell. White x a pure
// light colour is that colour, and lit_textured has no ambient term, so
// every red-cell centre stays exactly black. A golden alone could not
// tell "eight lights work" from "one light and a lucky image"; the tests
// below can.

using atlantis::image_regression::LightingDemoFixture;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderLightingDemoFrame;
using atlantis::image_regression::setUpLightingDemoFixture;
using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::kCameraUniformLightingOffsetBytes;
using atlantis::world::EntityId;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_multi_light_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_multi_light_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_multi_light_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

// Where a plane-local (x, z) point lands in the 512x512 frame. The camera
// sits 9 units in front of the plane (fov_y 60 degrees, aspect 1), so the
// frame spans +/- 9 * tan(30 deg) = +/- 5.196 plane units; plane-local -z
// is image-up (the plane is rotated +90 degrees about X to face the
// camera). Confirmed against a real capture during Implementation: every
// pool centre below measured exactly on this mapping.
[[nodiscard]] std::uint32_t planeToPixel(float planeCoordinate) {
  const float halfExtent = 9.0f * std::tan(0.5236f);
  return static_cast<std::uint32_t>(std::lround(256.0f + planeCoordinate * (256.0f / halfExtent) - 0.5f));
}

struct ExpectedPool {
  const char* name;
  float x;
  float z;
  bool redOn;
  bool greenOn;
  bool blueOn;
  bool greenPartial;  // orange only: color 1.0 0.4 0.0
};

// Scene order (node_id 3..10) -- also World::lightEntities() order, which
// is what makes the slot-destruction test below meaningful.
constexpr std::array<ExpectedPool, 8> kPools = {{
    {"red", -3.75f, -1.25f, true, false, false, false},
    {"green", -3.75f, 3.75f, false, true, false, false},
    {"blue", -1.25f, -3.75f, false, false, true, false},
    {"yellow", -1.25f, 1.25f, true, true, false, false},
    {"cyan", 1.25f, -1.25f, false, true, true, false},
    {"magenta", 1.25f, 3.75f, true, false, true, false},
    {"white", 3.75f, -3.75f, true, true, true, false},
    {"orange", 3.75f, 1.25f, true, false, false, true},
}};

// The eight red-cell centres: no light is above any of them.
constexpr std::array<std::array<float, 2>, 8> kDarkCells = {{
    {-3.75f, -3.75f},
    {-3.75f, 1.25f},
    {-1.25f, -1.25f},
    {-1.25f, 3.75f},
    {1.25f, -3.75f},
    {1.25f, 1.25f},
    {3.75f, -1.25f},
    {3.75f, 3.75f},
}};

// "On" means clearly lit (a pool centre measured 175); "off" means zero,
// give or take rounding -- nothing but a matching light can put energy in
// a channel here, because there is no ambient term.
constexpr int kOnThreshold = 100;
constexpr int kOffCeiling = 2;

[[nodiscard]] std::array<int, 3> rgbAt(const PixelBuffer& buffer, float x, float z) {
  const std::size_t offset = (static_cast<std::size_t>(planeToPixel(z)) * buffer.width + planeToPixel(x)) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2]};
}

void checkPoolLit(const PixelBuffer& frame, const ExpectedPool& pool) {
  const auto rgb = rgbAt(frame, pool.x, pool.z);
  INFO("pool " << pool.name << " rgb=(" << rgb[0] << ", " << rgb[1] << ", " << rgb[2] << ")");
  const bool on[3] = {pool.redOn, pool.greenOn, pool.blueOn};
  for (int channel = 0; channel < 3; ++channel) {
    if (channel == 1 && pool.greenPartial) {
      // Strictly between "off" and red's own level.
      CHECK(rgb[1] * 10 > rgb[0] * 3);
      CHECK(rgb[1] * 10 < rgb[0] * 9);
    } else if (on[channel]) {
      CHECK(rgb[channel] >= kOnThreshold);
    } else {
      CHECK(rgb[channel] <= kOffCeiling);
    }
  }
}

void checkRgbDark(const std::array<int, 3>& rgb) {
  CHECK(rgb[0] <= kOffCeiling);
  CHECK(rgb[1] <= kOffCeiling);
  CHECK(rgb[2] <= kOffCeiling);
}

}  // namespace

TEST_CASE("multi_light_demo: all eight Point lights reach the frame as eight distinct, pure, non-overlapping "
          "contributions, and every unlit cell stays black",
          "[image_regression][gpu][lighting][multi_light]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();
  REQUIRE(fixture.world->lightEntities().size() == kPools.size());

  auto renderResult = renderLightingDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& frame = renderResult.value();

  // The CPU side published all eight: more than the former cap of four.
  FrameLightingData published{};
  std::memcpy(&published,
              static_cast<const std::byte*>(fixture.cameraBuffer->mappedData()) + kCameraUniformLightingOffsetBytes,
              sizeof(FrameLightingData));
  REQUIRE(published.directionalLightCount == 0);
  REQUIRE(published.pointLightCount == 8);

  for (const ExpectedPool& pool : kPools) checkPoolLit(frame, pool);

  // Eight distinct colour signatures -- no two pools read the same.
  for (std::size_t a = 0; a < kPools.size(); ++a) {
    for (std::size_t b = a + 1; b < kPools.size(); ++b) {
      const auto ca = rgbAt(frame, kPools[a].x, kPools[a].z);
      const auto cb = rgbAt(frame, kPools[b].x, kPools[b].z);
      INFO(kPools[a].name << " vs " << kPools[b].name);
      CHECK(std::max({std::abs(ca[0] - cb[0]), std::abs(ca[1] - cb[1]), std::abs(ca[2] - cb[2])}) > 30);
    }
  }

  // No pool reaches a neighbouring cell, and nothing lights an empty one.
  for (const auto& cell : kDarkCells) {
    const auto rgb = rgbAt(frame, cell[0], cell[1]);
    INFO("dark cell (" << cell[0] << ", " << cell[1] << ") rgb=(" << rgb[0] << ", " << rgb[1] << ", " << rgb[2]
                       << ")");
    checkRgbDark(rgb);
  }

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("multi_light_demo: destroying the lights in slots 5-8 darkens exactly those four pools and leaves "
          "slots 1-4 untouched -- each slot above the former cap contributes on its own",
          "[image_regression][gpu][lighting][multi_light]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto beforeResult = renderLightingDemoFrame(fixture);
  REQUIRE(beforeResult.isOk());
  const PixelBuffer before = beforeResult.value();

  const std::vector<EntityId> lights = fixture.world->lightEntities();
  REQUIRE(lights.size() == 8);
  for (std::size_t slot = 4; slot < 8; ++slot) REQUIRE(fixture.world->destroyEntity(lights[slot]).isOk());

  auto afterResult = renderLightingDemoFrame(fixture);
  REQUIRE(afterResult.isOk());
  const PixelBuffer& after = afterResult.value();

  for (std::size_t slot = 0; slot < 4; ++slot) {
    INFO("kept slot " << slot + 1 << " (" << kPools[slot].name << ")");
    checkPoolLit(after, kPools[slot]);
    CHECK(rgbAt(after, kPools[slot].x, kPools[slot].z) == rgbAt(before, kPools[slot].x, kPools[slot].z));
  }
  for (std::size_t slot = 4; slot < 8; ++slot) {
    INFO("destroyed slot " << slot + 1 << " (" << kPools[slot].name << ")");
    checkPoolLit(before, kPools[slot]);
    checkRgbDark(rgbAt(after, kPools[slot].x, kPools[slot].z));
  }

  REQUIRE(fixture.device->waitIdle().isOk());
}
