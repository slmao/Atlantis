#include "fixture/lighting_demo_fixture.h"
#include "support/pixel_diff.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/light.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan 0019 Section P10/Milestone 10 (Spec 0019 D10, this Plan's own
// Milestone 9 requirements, delivered here since they need the real
// lighting_demo fixture -- see this Plan's own "Milestones / Task
// Breakdown" Section 10's explicit commit-by-commit description).
// Landed WITHOUT a golden yet, per ADR-0042's own two-phase process --
// every test case below is self-contained, never comparing against
// tests/image_regression/goldens/lighting_demo/ (that comparison, plus
// the deliberate direction-sign/point-attenuation-error negative tests
// that need it, land in a later, separate commit alongside the golden
// itself).

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::kLightingDemoExtentPixels;
using atlantis::image_regression::LightingDemoFixture;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderLightingDemoFrame;
using atlantis::image_regression::setUpLightingDemoFixture;
using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::computeLambertianDiffuse;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::extractFrameLightingData;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::LightExtractionInput;
using atlantis::runtime::Mat4;
using atlantis::runtime::Vec3;
using atlantis::world::EntityId;
using atlantis::world::Light;
using atlantis::world::Transform;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_lighting_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_lighting_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_lighting_demo_scene_MANIFEST_PATH;
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
  return config;
}

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

[[nodiscard]] bool nearBackgroundClearColor(const std::array<std::uint8_t, 4>& pixel) {
  // Renderer's own fixed kBackgroundClearColor (0.05, 0.05, 0.08, 1.0) ->
  // approximately (13, 13, 20, 255) in Rgba8Unorm bytes, matching
  // material_demo_gpu_tests.cpp's own identical constant.
  const int tolerance = 6;
  return std::abs(static_cast<int>(pixel[0]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[1]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[2]) - 20) <= tolerance;
}

[[nodiscard]] double coverageFraction(const PixelBuffer& frame) {
  std::size_t nonBackgroundCount = 0;
  std::size_t sampledCount = 0;
  for (std::uint32_t y = 0; y < frame.height; y += 4) {
    for (std::uint32_t x = 0; x < frame.width; x += 4) {
      ++sampledCount;
      if (!nearBackgroundClearColor(pixelAt(frame, x, y))) ++nonBackgroundCount;
    }
  }
  return sampledCount > 0 ? static_cast<double>(nonBackgroundCount) / static_cast<double>(sampledCount) : 0.0;
}

struct Vec4 {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// Column-major Mat4 (matches Mat4's own established convention
// throughout scene_extraction.cpp/world.cpp): column c occupies indices
// [c*4, c*4+3].
[[nodiscard]] Vec4 transformVec4(const Mat4& m, const Vec4& v) {
  return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w, m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
          m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w, m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
}

[[nodiscard]] Vec4 transformPoint(const Mat4& m, const Vec3& p) {
  return transformVec4(m, Vec4{p.x, p.y, p.z, 1.0f});
}

}  // namespace

TEST_CASE("Lighting demo fixture renders a non-degenerate frame with real lit-cube coverage",
          "[image_regression][gpu][lighting]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderLightingDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kLightingDemoExtentPixels);
  REQUIRE(frame.height == kLightingDemoExtentPixels);

  for (std::uint32_t y = 0; y < frame.height; y += 16) {
    for (std::uint32_t x = 0; x < frame.width; x += 16) {
      CHECK(pixelAt(frame, x, y)[3] == 255);
    }
  }

  CHECK(coverageFraction(frame) > 0.01);
}

TEST_CASE("LightingDemoFixture: World::setLight() after the one-time capture changes World state but never the "
          "already-published GPU FrameLightingData bytes",
          "[image_regression][gpu][lighting]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto firstResult = renderLightingDemoFrame(fixture);
  REQUIRE(firstResult.isOk());
  REQUIRE(fixture.lightingDataCaptured);
  const PixelBuffer firstPixels = firstResult.value();

  // Snapshot the published FrameLightingData tail bytes (absolute
  // offset 128, 176 bytes) directly from the mapped, host-visible
  // Buffer -- no GPU readback needed.
  const auto* cameraBytes = static_cast<const std::byte*>(fixture.cameraBuffer->mappedData());
  std::array<std::byte, sizeof(FrameLightingData)> bytesBefore{};
  std::memcpy(bytesBefore.data(), cameraBytes + sizeof(float) * 32, bytesBefore.size());

  const std::vector<EntityId> lights = fixture.world->lightEntities();
  REQUIRE_FALSE(lights.empty());
  const EntityId targetLight = lights.front();
  const auto originalLightResult = fixture.world->getLight(targetLight);
  REQUIRE(originalLightResult.isOk());

  // A drastic, unmistakable mutation -- never anything close to the
  // original value.
  Light mutated = originalLightResult.value();
  mutated.color = {0.0f, 0.0f, 0.0f};
  mutated.intensity = 999.0f;
  REQUIRE(fixture.world->setLight(targetLight, mutated).isOk());

  // CPU/World state genuinely changed...
  const auto changedLightResult = fixture.world->getLight(targetLight);
  REQUIRE(changedLightResult.isOk());
  CHECK(changedLightResult.value().intensity == 999.0f);

  // ...but the published GPU bytes did not, bit-for-bit, even before a
  // second render call.
  std::array<std::byte, sizeof(FrameLightingData)> bytesAfterMutationOnly{};
  std::memcpy(bytesAfterMutationOnly.data(), cameraBytes + sizeof(float) * 32, bytesAfterMutationOnly.size());
  CHECK(bytesBefore == bytesAfterMutationOnly);

  // A second render call -- lightingDataCaptured is already true, so no
  // recapture happens; the rendered pixels must be byte-identical to
  // the first render's own (nothing else in this static scene changes
  // between the two calls).
  auto secondResult = renderLightingDemoFrame(fixture);
  REQUIRE(secondResult.isOk());
  CHECK(secondResult.value().rgba8 == firstPixels.rgba8);

  std::array<std::byte, sizeof(FrameLightingData)> bytesAfterSecondRender{};
  std::memcpy(bytesAfterSecondRender.data(), cameraBytes + sizeof(float) * 32, bytesAfterSecondRender.size());
  CHECK(bytesBefore == bytesAfterSecondRender);
}

TEST_CASE("LightingDemoFixture: a LitTextured-bound entity given a deliberately non-conformal world transform is "
          "skipped for that frame's own DrawItem list, never scene-load/frame-fatal",
          "[image_regression][gpu][lighting]") {
  // Baseline: confirm the scene's own single Renderable normally covers
  // a real, meaningful fraction of the frame.
  auto baselineFixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(baselineFixtureResult.isOk());
  LightingDemoFixture& baselineFixture = baselineFixtureResult.value();
  auto baselineResult = renderLightingDemoFrame(baselineFixture);
  REQUIRE(baselineResult.isOk());
  REQUIRE(coverageFraction(baselineResult.value()) > 0.01);
  REQUIRE(baselineFixture.world->renderableEntities().size() == 1);

  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  const std::vector<EntityId> renderables = fixture.world->renderableEntities();
  REQUIRE(renderables.size() == 1);
  const EntityId litEntity = renderables.front();
  auto transformResult = fixture.world->getLocalTransform(litEntity);
  REQUIRE(transformResult.isOk());
  Transform nonConformal = transformResult.value();
  nonConformal.localScale = {2.0f, 1.0f, 1.0f};  // non-uniform scale -- D7's own rejected case
  REQUIRE(fixture.world->setLocalTransform(litEntity, nonConformal).isOk());

  auto renderResult = renderLightingDemoFrame(fixture);
  REQUIRE(renderResult.isOk());  // never scene-load/frame-fatal -- a recoverable, per-entity skip

  // The cube is completely absent -- this scene's own single Renderable
  // was the only entity ever eligible to draw, and it was skipped.
  CHECK(coverageFraction(renderResult.value()) == 0.0);
}

TEST_CASE("LightingDemoFixture: realizing the LitTextured material via the wrong MaterialKind (as if "
          "UnlitTextured) produces a visibly, meaningfully different result from the correctly-dispatched case",
          "[image_regression][gpu][lighting]") {
  auto correctFixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(correctFixtureResult.isOk());
  LightingDemoFixture& correctFixture = correctFixtureResult.value();
  auto correctRenderResult = renderLightingDemoFrame(correctFixture);
  REQUIRE(correctRenderResult.isOk());
  const PixelBuffer correctPixels = correctRenderResult.value();

  auto wrongFixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(wrongFixtureResult.isOk());
  LightingDemoFixture& wrongFixture = wrongFixtureResult.value();
  // Deliberate, test-only misconfiguration: force every material's own
  // materialDataMap entry to report UnlitTextured, so selectShaderPair()
  // (material_realization.cpp) dispatches the WRONG shader pair for
  // what is really a LitTextured-authored material.
  for (auto& [id, data] : wrongFixture.materialDataMap) {
    data.kind = atlantis::asset_system::MaterialKind::UnlitTextured;
  }
  auto wrongRenderResult = renderLightingDemoFrame(wrongFixture);
  REQUIRE(wrongRenderResult.isOk());
  const PixelBuffer wrongPixels = wrongRenderResult.value();

  const auto report = compareBuffers(correctPixels, wrongPixels);
  CHECK_FALSE(report.passed);
  // A real, meaningfully different result, not merely a rounding/AA
  // difference (which compareBuffers()'s own zero-tolerance ADR-0042
  // budget would already catch on its own): the unlit-dispatched cube
  // ignores lighting entirely (flat, unmodulated texture color) while
  // the correctly-dispatched cube is modulated by the real Lambertian
  // accumulation -- a real, large per-channel difference on the cube's
  // own lit surface.
  CHECK(report.maxChannelDiff > 20);
}

TEST_CASE("LightingDemoFixture: a captured pixel at a known cube vertex matches computeLambertianDiffuse()'s own "
          "independently-computed expected lit color, within a disclosed tolerance",
          "[image_regression][gpu][lighting]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto renderResult = renderLightingDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& frame = renderResult.value();

  // The scene's own real, fixed values (assets/scenes/lighting_demo.scene.txt):
  // the cube (node 1) has an identity world matrix (position/rotation
  // zero, scale one), so a point interior to one of its own triangles
  // (assets/meshes/minimal_cube.mesh.txt) computed in local space is
  // also its own world-space value, unmodified by this test.
  // Deliberately NOT a vertex position: a shared cube corner sits
  // exactly on a silhouette/triangle-edge boundary, where a
  // hand-computed screen pixel can legitimately land just outside the
  // real rasterized triangle (confirmed by an earlier attempt at this
  // exact test, which sampled the untouched background there). The
  // centroid of the near face's own first triangle (index list
  // "5 4 7") is a point strictly interior to a real, single triangle --
  // never an edge/corner case -- so its own barycentric-interpolated
  // normal is exactly the unweighted average of that triangle's three
  // vertex normals (barycentric coordinates (1/3, 1/3, 1/3)):
  // v5=(0.5,-0.5,0.5)/n5=(0.577350269,-0.577350269,0.577350269),
  // v4=(-0.5,-0.5,0.5)/n4=(-0.577350269,-0.577350269,0.577350269),
  // v7=(-0.5,0.5,0.5)/n7=(-0.577350269,0.577350269,0.577350269).
  const Vec3 worldPosition{(0.5f - 0.5f - 0.5f) / 3.0f, (-0.5f - 0.5f + 0.5f) / 3.0f, 0.5f};
  const Vec3 worldNormal{(0.577350269f - 0.577350269f - 0.577350269f) / 3.0f,
                          (-0.577350269f - 0.577350269f + 0.577350269f) / 3.0f, 0.577350269f};

  const auto activeCamera = fixture.world->activeCamera();
  REQUIRE(activeCamera.has_value());
  const auto cameraWorldMatrixResult = fixture.world->getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world->getCamera(*activeCamera);
  REQUIRE(cameraWorldMatrixResult.isOk());
  REQUIRE(cameraComponentResult.isOk());
  // The real, shared extractCameraMatrices() -- camera projection math
  // is not this Plan's own cross-validation subject (ADR-0051/Plan
  // 0014 already establish it); calling it here is legitimate reuse,
  // not a shortcut around the lighting-math cross-check below.
  const auto cameraMatricesResult =
      extractCameraMatrices(cameraWorldMatrixResult.value(), cameraComponentResult.value().fovYRadians,
                             cameraComponentResult.value().nearZ, cameraComponentResult.value().farZ, 1.0f);
  REQUIRE(cameraMatricesResult.isOk());

  const Vec4 viewPos = transformPoint(cameraMatricesResult.value().view, worldPosition);
  const Vec4 clipPos = transformVec4(cameraMatricesResult.value().projection, viewPos);
  REQUIRE(clipPos.w != 0.0f);
  const float ndcX = clipPos.x / clipPos.w;
  const float ndcY = clipPos.y / clipPos.w;
  const auto screenX = static_cast<std::uint32_t>(
      std::lround((ndcX * 0.5f + 0.5f) * static_cast<float>(kLightingDemoExtentPixels)));
  const auto screenY = static_cast<std::uint32_t>(
      std::lround((ndcY * 0.5f + 0.5f) * static_cast<float>(kLightingDemoExtentPixels)));
  REQUIRE(screenX < kLightingDemoExtentPixels);
  REQUIRE(screenY < kLightingDemoExtentPixels);

  // The real light parameters, extracted via the real, shared
  // extractFrameLightingData() -- called here (never reimplemented) to
  // build the expected FrameLightingData this pixel's own lighting is
  // computed from.
  std::vector<LightExtractionInput> lightInputs;
  for (const auto& id : fixture.world->lightEntities()) {
    const auto lightResult = fixture.world->getLight(id);
    const auto lightWorldMatrixResult = fixture.world->getWorldMatrix(id);
    REQUIRE(lightResult.isOk());
    REQUIRE(lightWorldMatrixResult.isOk());
    lightInputs.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  REQUIRE(lightingResult.isOk());

  // texColor: the real, Vulkan-standard bilinear+repeat sample at
  // UV (0,0) -- exactly the average of the texture's own four corner
  // texels (a well-defined consequence of linear filtering + repeat
  // addressing sampled exactly at a texel-grid corner: fx = fy = 0.5,
  // blending texel(W-1,H-1)/texel(0,H-1)/texel(W-1,0)/texel(0,0) at
  // equal 0.25 weights). Every vertex on minimal_cube shares the
  // identical deterministic UV placeholder (0,0) -- assets/CMakeLists.txt's
  // own comment. Read directly from the real, Phase-1-loaded
  // TextureAssetData, never re-decoded from the PNG file.
  REQUIRE(fixture.materialDataMap.size() == 1);
  const auto textureAssetId = fixture.materialDataMap.begin()->second.textureAsset;
  const auto textureIt = fixture.textureDataMap.find(textureAssetId);
  REQUIRE(textureIt != fixture.textureDataMap.end());
  const auto& textureData = textureIt->second;
  REQUIRE(textureData.width > 0);
  REQUIRE(textureData.height > 0);
  const auto texelAt = [&](std::uint32_t x, std::uint32_t y) -> std::array<float, 4> {
    const std::size_t offset = (static_cast<std::size_t>(y) * textureData.width + x) * 4;
    return {textureData.pixelBytes[offset] / 255.0f, textureData.pixelBytes[offset + 1] / 255.0f,
            textureData.pixelBytes[offset + 2] / 255.0f, textureData.pixelBytes[offset + 3] / 255.0f};
  };
  const auto c00 = texelAt(0, 0);
  const auto c10 = texelAt(textureData.width - 1, 0);
  const auto c01 = texelAt(0, textureData.height - 1);
  const auto c11 = texelAt(textureData.width - 1, textureData.height - 1);
  const Vec3 texColor{(c00[0] + c10[0] + c01[0] + c11[0]) * 0.25f, (c00[1] + c10[1] + c01[1] + c11[1]) * 0.25f,
                       (c00[2] + c10[2] + c01[2] + c11[2]) * 0.25f};

  const Vec3 accumulated = computeLambertianDiffuse(worldPosition, worldNormal, lightingResult.value());
  const auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
  const std::array<std::uint8_t, 3> expectedRgb{
      static_cast<std::uint8_t>(std::lround(clamp01(texColor.x * accumulated.x) * 255.0f)),
      static_cast<std::uint8_t>(std::lround(clamp01(texColor.y * accumulated.y) * 255.0f)),
      static_cast<std::uint8_t>(std::lround(clamp01(texColor.z * accumulated.z) * 255.0f))};

  const auto actualPixel = pixelAt(frame, screenX, screenY);
  // A disclosed tolerance -- GPU-side float execution, triangle-
  // rasterization-boundary-adjacent sampling, and this point sitting
  // exactly at a shared-vertex-normal corner (three triangles meet
  // here) are none of them required to be bit-identical to this
  // CPU-computed reference.
  constexpr int kTolerancePerChannel = 12;
  CHECK(std::abs(static_cast<int>(actualPixel[0]) - static_cast<int>(expectedRgb[0])) <= kTolerancePerChannel);
  CHECK(std::abs(static_cast<int>(actualPixel[1]) - static_cast<int>(expectedRgb[1])) <= kTolerancePerChannel);
  CHECK(std::abs(static_cast<int>(actualPixel[2]) - static_cast<int>(expectedRgb[2])) <= kTolerancePerChannel);
}

TEST_CASE("LightingDemoFixture: repeated render cycles against the same fixture succeed independently",
          "[image_regression][gpu][lighting]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto firstResult = renderLightingDemoFrame(fixture);
  REQUIRE(firstResult.isOk());
  auto secondResult = renderLightingDemoFrame(fixture);
  REQUIRE(secondResult.isOk());

  CHECK(firstResult.value().rgba8 == secondResult.value().rgba8);
  CHECK(fixture.materialResourceMap.size() == 1);
}
