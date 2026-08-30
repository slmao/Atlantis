#include "fixture/lighting_demo_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/light.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
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
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderLightingDemoFrame;
using atlantis::image_regression::setUpLightingDemoFixture;
using atlantis::image_regression::writeFailureArtifacts;
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

TEST_CASE("LightingDemoFixture: World::setLight() before a second render call changes both the published GPU "
          "FrameLightingData bytes and the rendered pixels -- Plan 0022's own corrected, dynamic contract",
          "[image_regression][gpu][lighting]") {
  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto firstResult = renderLightingDemoFrame(fixture);
  REQUIRE(firstResult.isOk());
  const PixelBuffer firstPixels = firstResult.value();

  // Snapshot the published FrameLightingData tail bytes (absolute
  // offset 128, 176 bytes) directly from the mapped, host-visible
  // Buffer -- no GPU readback needed.
  const auto* cameraBytes = static_cast<const std::byte*>(fixture.cameraBuffer->mappedData());
  std::array<std::byte, sizeof(FrameLightingData)> bytesAfterFirstRender{};
  std::memcpy(bytesAfterFirstRender.data(), cameraBytes + sizeof(float) * 32, bytesAfterFirstRender.size());

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

  // ...and, unlike Spec 0019's own original one-time-capture contract
  // this Spec 0022/Plan 0022 supersede, the mutation IS reflected: a
  // second render call re-extracts and republishes the complete
  // FrameLightingData from World's own current state, so both the
  // published bytes and the rendered pixels change to reflect it.
  auto secondResult = renderLightingDemoFrame(fixture);
  REQUIRE(secondResult.isOk());
  CHECK_FALSE(secondResult.value().rgba8 == firstPixels.rgba8);

  std::array<std::byte, sizeof(FrameLightingData)> bytesAfterSecondRender{};
  std::memcpy(bytesAfterSecondRender.data(), cameraBytes + sizeof(float) * 32, bytesAfterSecondRender.size());
  CHECK_FALSE(bytesAfterFirstRender == bytesAfterSecondRender);

  // Precisely: the published intensity field for this light's own slot
  // now matches the mutated value, not the original one -- reinterpret
  // the raw snapshot back through the real, shared FrameLightingData
  // layout (never through hand-computed byte offsets, which would risk
  // silently drifting from the real, static_assert-locked layout).
  FrameLightingData publishedLighting{};
  std::memcpy(&publishedLighting, bytesAfterSecondRender.data(), sizeof(FrameLightingData));
  const float publishedIntensity = changedLightResult.value().kind == atlantis::world::LightKind::Directional
                                        ? publishedLighting.directionalLights[0].intensity
                                        : publishedLighting.pointLights[0].intensity;
  CHECK(publishedIntensity == 999.0f);
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

// ---------------------------------------------------------------------
// Plan 0019 Milestone 10b: the golden itself, captured on this Plan's
// own already-committed, clean Milestone 10a implementation (ADR-0042's
// own two-phase process) -- and the two deliberate-error negative tests
// that need it (Milestone 9's own "direction-sign error"/"point
// position-or-attenuation error" requirements): each proves this Plan's
// own D6 sign convention/attenuation formula is genuinely load-bearing,
// not merely stated in prose, by showing a real, deliberately-wrong
// scene fails comparison against the real, human-reviewed golden.
// ---------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_lighting_demo_gpu_tests" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// A direct transcription of assets/scenes/lighting_demo.scene.txt's own
// real, committed content, with the Directional light's own yaw and the
// Point light's own range replaced by a caller-supplied token -- every
// other value (mesh, material, camera, colors, intensities, the Point
// light's own position) stays byte-identical to the real scene, so each
// negative test below changes exactly the one value its own name
// describes, nothing else.
constexpr std::string_view kLightingDemoSourceTemplate =
    "atlantis_scene_source_version: 3\n"
    "node_count: 4\n"
    "active_camera: 2\n"
    "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "mesh=meshes/minimal_cube.mesh.txt material=materials/lit_textured_quad.material.txt\n"
    "node: node_id=2 parent=none position=0.0 0.0 5.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n"
    "node: node_id=3 parent=none position=0.0 0.0 0.0 rotation=0.5 {DIRECTIONAL_YAW} 0.0 scale=1.0 1.0 1.0 "
    "light=directional color=0.6 0.7 1.0 intensity=1.2\n"
    "node: node_id=4 parent=none position=0.8 0.3 0.5 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "light=point color=1.0 0.6 0.3 intensity=3.0 range={POINT_RANGE}\n";

[[nodiscard]] std::string replaceOnce(std::string text, std::string_view token, const std::string& value) {
  const auto pos = text.find(token);
  REQUIRE(pos != std::string::npos);
  text.replace(pos, token.size(), value);
  return text;
}

// Cooks a deliberately-mutated variant of lighting_demo.scene.txt's own
// content into a temp directory, reusing the REAL, already-cooked mesh/
// material/texture dependencies verbatim -- their own build-tree
// artifact paths never change, only the scene's own light values do, so
// the real, already-generated manifest (config.sceneDependencyManifestPath)
// is copied unchanged rather than rebuilt.
[[nodiscard]] BootstrapConfig buildMutatedTestConfig(const fs::path& dir, const std::string& mutatedSourceText) {
  const fs::path sourcePath = dir / "lighting_demo_mutated.scene.txt";
  writeFile(sourcePath, mutatedSourceText);
  const fs::path artifactPath = dir / "lighting_demo_mutated.ascene";
  const fs::path metadataPath = dir / "lighting_demo_mutated.ascene.meta.txt";
  REQUIRE(
      atlantis::asset_system::cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());

  const fs::path manifestPath = dir / "manifest.txt";
  std::ifstream realManifest(std::string(ATLANTIS_lighting_demo_scene_MANIFEST_PATH), std::ios::binary);
  std::ostringstream manifestBuffer;
  manifestBuffer << realManifest.rdbuf();
  writeFile(manifestPath, manifestBuffer.str());

  BootstrapConfig config = buildTestConfig();
  config.sceneArtifactPath = artifactPath.string();
  config.sceneMetadataPath = metadataPath.string();
  config.sceneDependencyManifestPath = manifestPath.string();
  return config;
}

constexpr const char* kLightingDemoGoldenName = "lighting_demo/lighting_demo_512x512_rgba8unorm";
constexpr const char* kLightingDemoGoldenSlug = "lighting_demo_512x512_rgba8unorm";

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed lighting_demo golden passes",
          "[image_regression][gpu][lighting]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kLightingDemoGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kLightingDemoGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpLightingDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();

  auto renderResult = renderLightingDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = loadAndValidateGolden(goldensDir / (std::string(kLightingDemoGoldenName) + ".png"),
                                             goldensDir / (std::string(kLightingDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed lighting_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kLightingDemoGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("A deliberate Directional light direction-sign error produces a captured frame that fails comparison "
          "against the real lighting_demo golden",
          "[image_regression][gpu][lighting]") {
  TempDirGuard dir("direction_sign_error");
  // yaw + pi exactly negates the extracted direction vector (Spec 0019
  // D2's own -column2 formula, Milestone 7's own extractFrameLightingData()):
  // Ry(yaw + pi) * localZ = Ry(yaw) * Ry(pi) * localZ = Ry(yaw) * (-localZ)
  // = -(Ry(yaw) * localZ), since Ry(pi) maps local +Z to local -Z and
  // leaves Y untouched -- a real, deliberate 180-degree flip of the
  // light's own authored direction, not merely "a different rotation".
  const std::string mutatedSource =
      replaceOnce(replaceOnce(std::string(kLightingDemoSourceTemplate), "{DIRECTIONAL_YAW}", "2.5415927"),
                  "{POINT_RANGE}", "2.5");
  const BootstrapConfig config = buildMutatedTestConfig(dir.path, mutatedSource);

  auto fixtureResult = setUpLightingDemoFixture(config);
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();
  auto renderResult = renderLightingDemoFrame(fixture);
  REQUIRE(renderResult.isOk());

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = loadAndValidateGolden(goldensDir / (std::string(kLightingDemoGoldenName) + ".png"),
                                             goldensDir / (std::string(kLightingDemoGoldenName) + ".sidecar.txt"));
  REQUIRE(goldenResult.isOk());

  const auto report = compareBuffers(renderResult.value(), goldenResult.value().pixels);
  CHECK_FALSE(report.passed);
  CHECK(report.maxChannelDiff > 20);
}

TEST_CASE("A deliberate Point light attenuation error (an implausibly small range) produces a captured frame that "
          "fails comparison against the real lighting_demo golden",
          "[image_regression][gpu][lighting]") {
  TempDirGuard dir("point_attenuation_error");
  // range=0.01 clamps atten = clamp(1 - dist/range, 0, 1) to exactly 0
  // at any real distance from the cube's own surface (the closest real
  // surface point is well over 0.01 units from (0.8, 0.3, 0.5)) --
  // the Point light's own real, visible orange hotspot in the golden
  // vanishes entirely.
  const std::string mutatedSource =
      replaceOnce(replaceOnce(std::string(kLightingDemoSourceTemplate), "{DIRECTIONAL_YAW}", "-0.6"), "{POINT_RANGE}",
                  "0.01");
  const BootstrapConfig config = buildMutatedTestConfig(dir.path, mutatedSource);

  auto fixtureResult = setUpLightingDemoFixture(config);
  REQUIRE(fixtureResult.isOk());
  LightingDemoFixture& fixture = fixtureResult.value();
  auto renderResult = renderLightingDemoFrame(fixture);
  REQUIRE(renderResult.isOk());

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = loadAndValidateGolden(goldensDir / (std::string(kLightingDemoGoldenName) + ".png"),
                                             goldensDir / (std::string(kLightingDemoGoldenName) + ".sidecar.txt"));
  REQUIRE(goldenResult.isOk());

  const auto report = compareBuffers(renderResult.value(), goldenResult.value().pixels);
  CHECK_FALSE(report.passed);
  CHECK(report.maxChannelDiff > 20);
}
